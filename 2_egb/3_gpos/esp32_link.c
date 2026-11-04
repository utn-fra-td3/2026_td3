#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/string.h>

#include "esp32_link.h"

#define DEVICE_NAME "esp32link"
#define CLASS_NAME  "td3"
#define MAX_LINE    64

/* A 921600 baud llegan ~92 KB/s de tramas binarias. kfifo_alloc() redondea
 * a potencia de 2; 16 KB son ~20 tramas de 816 bytes de margen. */
#define FIFO_SIZE   16384

/* Trama del ESP32 (packed, little endian), 816 bytes:
 *   0-1   sync 0xAA55 (bytes 55 AA)
 *   2     flanco       (0 = subida, 1 = bajada)
 *   3-4   nivel        (0..4095)
 *   5-6   tiempo       (decimas de ms: 5, 10, 20, 50)
 *   7-10  amplitud     (float IEEE754: 1.0 = 0x3F800000, 5.0 = 0x40A00000)
 *   11    modo         (0 = X1, 1 = X10, 2 = AC)
 *   12-13 num_puntos   (400)
 *   14..813 datos      (400 muestras de 16 bits)
 *   814-815 checksum   (suma de bytes 0..813, 16 bits)
 */
#define FRAME_LEN        816
#define FRAME_CHK_OFF    814
#define AMP_1V_BITS      0x3F800000u
#define AMP_5V_BITS      0x40A00000u

/* Cuanto espera ESP32_GET a la proxima trama. El ESP32 manda una cada
 * ~150-200 ms, 1 s da margen de sobra. */
#define GET_TIMEOUT_MS   1000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TD3 -- UTN FRA");
MODULE_DESCRIPTION("Char device sobre serdev: comandos de texto hacia un ESP32-S3 y tramas binarias de vuelta");

static dev_t           dev_num;
static struct cdev     esp32_cdev;
static struct class   *esp32_class;
static struct device  *esp32_device;
static struct serdev_device *esp32_serdev;

/* read() como productor-consumidor: receive_buf() (productor, unico) empuja
 * los bytes crudos que llegan por la UART; esp32_read() (consumidor, unico
 * mientras haya un solo lector) los saca. El stream sigue pasando entero
 * hacia la aplicacion (Python); el driver ADEMAS lo mira de reojo para
 * recordar el estado vigente y poder contestar ESP32_GET. */
static struct kfifo rx_fifo;
static DECLARE_WAIT_QUEUE_HEAD(rx_wait);

/* ===================== estado vigente (para GET) ===================== */

struct esp32_estado {
    bool valido;        /* true cuando ya llego al menos una trama buena */
    u8   flanco;
    u16  nivel;
    u16  tiempo;        /* decimas de ms */
    u32  amplitud_bits; /* float crudo; el kernel no usa punto flotante */
    u8   modo;
    u32  seq;           /* se incrementa con cada trama valida */
};

static struct esp32_estado estado;
static DEFINE_SPINLOCK(estado_lock);
static DECLARE_WAIT_QUEUE_HEAD(estado_wait);

static u8           frame[FRAME_LEN];
static unsigned int frame_len;

/* BEGIN_PARSER (este bloque se prueba tambien en espacio de usuario) */
static void frame_publicar(void)
{
    unsigned long flags;

    spin_lock_irqsave(&estado_lock, flags);
    estado.flanco        = frame[2];
    estado.nivel         = frame[3] | (frame[4] << 8);
    estado.tiempo        = frame[5] | (frame[6] << 8);
    estado.amplitud_bits = frame[7] | (frame[8] << 8) |
                           (frame[9] << 16) | ((u32)frame[10] << 24);
    estado.modo          = frame[11];
    estado.valido        = true;
    estado.seq++;
    spin_unlock_irqrestore(&estado_lock, flags);

    wake_up_interruptible(&estado_wait);
}

/* Checksum malo: el 55 AA con el que empezamos era falso (basura, o la
 * trama arranco a medias). Antes de tirar todo, buscamos otro 55 AA dentro
 * de lo ya acumulado: ahi puede estar el comienzo de una trama real. */
static void frame_resync(void)
{
    unsigned int i;

    for (i = 1; i + 1 < FRAME_LEN; i++) {
        if (frame[i] == 0x55 && frame[i + 1] == 0xAA) {
            memmove(frame, &frame[i], FRAME_LEN - i);
            frame_len = FRAME_LEN - i;
            return;
        }
    }

    /* Sin 55 AA adentro: el ultimo byte podria ser el 0x55 de un sync que viene */
    if (frame[FRAME_LEN - 1] == 0x55) {
        frame[0] = 0x55;
        frame_len = 1;
    } else {
        frame_len = 0;
    }
}

/* Maquina de estados byte a byte: busca 55 AA, junta 816 bytes, verifica
 * el checksum y recien ahi publica el estado. Si el checksum no cierra,
 * intenta resincronizar (frame_resync). */
static void frame_feed(u8 b)
{
    unsigned int i;
    u16 chk = 0, chk_rx;

    switch (frame_len) {
    case 0:
        if (b == 0x55)
            frame[frame_len++] = b;
        return;
    case 1:
        if (b == 0xAA)
            frame[frame_len++] = b;
        else if (b != 0x55)
            frame_len = 0;      /* si fue otro 0x55, frame[0] ya lo tiene */
        return;
    default:
        frame[frame_len++] = b;
        break;
    }

    if (frame_len < FRAME_LEN)
        return;

    for (i = 0; i < FRAME_CHK_OFF; i++)
        chk += frame[i];
    chk_rx = frame[FRAME_CHK_OFF] | (frame[FRAME_CHK_OFF + 1] << 8);

    if (chk == chk_rx) {
        frame_len = 0;
        frame_publicar();
    } else {
        frame_resync();
    }
}
/* END_PARSER */

/* ===================== serdev: recepcion ===================== */

static size_t esp32_receive_buf(struct serdev_device *serdev,
                                 const unsigned char *data, size_t count)
{
    size_t i;

    /* 1) el stream completo sigue yendo a la aplicacion */
    kfifo_in(&rx_fifo, data, count);
    wake_up_interruptible(&rx_wait);

    /* 2) y de paso actualizamos el estado vigente para ESP32_GET */
    for (i = 0; i < count; i++)
        frame_feed(data[i]);

    return count; /* consumimos todo lo que nos dieron */
}

/* serdev_device_write() exige un write_wakeup registrado para aceptar un
 * timeout != 0. No hace falta hacer nada aca, alcanza con que exista. */
static void esp32_write_wakeup(struct serdev_device *serdev)
{
}

static const struct serdev_device_ops esp32_serdev_ops = {
    .receive_buf  = esp32_receive_buf,
    .write_wakeup = esp32_write_wakeup,
};

/* ===================== file_operations ===================== */

static int esp32_open(struct inode *inode, struct file *file)
{
    /* Descartamos lo acumulado mientras nadie leia, para no arrancar con
     * tramas viejas. reset_out() es seguro frente al productor. */
    kfifo_reset_out(&rx_fifo);
    return 0;
}

static int esp32_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t esp32_read(struct file *file, char __user *buf,
                           size_t len, loff_t *off)
{
    unsigned int copiados;
    int ret;

    if (kfifo_is_empty(&rx_fifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(rx_wait, !kfifo_is_empty(&rx_fifo)))
            return -ERESTARTSYS;
    }

    ret = kfifo_to_user(&rx_fifo, buf, len, &copiados);
    if (ret)
        return ret;

    return copiados;
}

static ssize_t esp32_write(struct file *file, const char __user *buf,
                            size_t len, loff_t *off)
{
    char   local[MAX_LINE];
    size_t n = len;
    int    ret;

    if (n >= sizeof(local))
        n = sizeof(local) - 1; /* nunca copiar mas de lo que entra en local[] */

    if (copy_from_user(local, buf, n))
        return -EFAULT;
    local[n] = '\0';

    /* Asegurar el delimitador de linea que espera el firmware, sin pisar
     * el limite del buffer local. */
    if ((n == 0 || local[n - 1] != '\n') && n < sizeof(local) - 1) {
        local[n++] = '\n';
        local[n] = '\0';
    }

    ret = serdev_device_write(esp32_serdev, local, n, msecs_to_jiffies(1000));
    if (ret < 0) {
        printk(KERN_ERR "esp32_link: serdev_device_write() fallo en write() (%d)\n", ret);
        return ret;
    }
    if (ret != n) {
        printk(KERN_WARNING "esp32_link: write parcial, se mandaron %d de %zu bytes\n",
               ret, n);
    }

    return len; /* se consumio todo el buffer original */
}

/* ESP32_GET: espera la PROXIMA trama (asi el valor refleja cambios recien
 * hechos con set o con el encoder) y devuelve la variable pedida con la
 * misma convencion que usa set: M, F, T y A son indices; L es el nivel. */
static long esp32_ioctl_get(struct esp32_var __user *argp)
{
    struct esp32_var var;
    struct esp32_estado e;
    unsigned long flags;
    u32 seq0;
    long ret;

    if (copy_from_user(&var, argp, sizeof(var)))
        return -EFAULT;
    var.nombre[sizeof(var.nombre) - 1] = '\0';

    spin_lock_irqsave(&estado_lock, flags);
    seq0 = estado.seq;
    spin_unlock_irqrestore(&estado_lock, flags);

    ret = wait_event_interruptible_timeout(estado_wait,
                READ_ONCE(estado.seq) != seq0,
                msecs_to_jiffies(GET_TIMEOUT_MS));
    if (ret == 0)
        return -ETIMEDOUT;  /* no llegaron tramas: ESP32 apagado o desconectado */
    if (ret < 0)
        return ret;         /* -ERESTARTSYS: la llamada se interrumpio */

    spin_lock_irqsave(&estado_lock, flags);
    e = estado;
    spin_unlock_irqrestore(&estado_lock, flags);

    if (!strcmp(var.nombre, "M")) {
        var.valor = e.modo;
    } else if (!strcmp(var.nombre, "F")) {
        var.valor = e.flanco;
    } else if (!strcmp(var.nombre, "L")) {
        var.valor = e.nivel;
    } else if (!strcmp(var.nombre, "T")) {
        switch (e.tiempo) {
        case 5:  var.valor = 0; break;
        case 10: var.valor = 1; break;
        case 20: var.valor = 2; break;
        case 50: var.valor = 3; break;
        default: return -EPROTO;
        }
    } else if (!strcmp(var.nombre, "A")) {
        if (e.amplitud_bits == AMP_1V_BITS)
            var.valor = 0;
        else if (e.amplitud_bits == AMP_5V_BITS)
            var.valor = 1;
        else
            return -EPROTO;
    } else {
        return -EINVAL;     /* variable desconocida */
    }

    return copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
}

static long esp32_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct esp32_var __user *argp = (struct esp32_var __user *)arg;
    struct esp32_var var;
    char msg[48];
    int  ret;

    switch (cmd) {
    case ESP32_SET:
        if (copy_from_user(&var, argp, sizeof(var)))
            return -EFAULT;
        var.nombre[sizeof(var.nombre) - 1] = '\0';
        snprintf(msg, sizeof(msg), "set %s %d\n", var.nombre, var.valor);
        ret = serdev_device_write(esp32_serdev, msg, strlen(msg),
                                  msecs_to_jiffies(100));
        return (ret < 0) ? ret : 0;

    case ESP32_GET:
        return esp32_ioctl_get(argp);

    default:
        return -ENOTTY; /* comando no reconocido: convencion POSIX */
    }
}

static const struct file_operations esp32_fops = {
    .owner          = THIS_MODULE,
    .open           = esp32_open,
    .release        = esp32_release,
    .read           = esp32_read,
    .write          = esp32_write,
    .unlocked_ioctl = esp32_ioctl,
};

/* ===================== serdev: probe/remove ===================== */

static int esp32_probe(struct serdev_device *serdev)
{
    u32 baudrate = 921600; /* valor por defecto si el DT no trae current-speed */
    int ret;

    esp32_serdev = serdev;
    frame_len = 0;
    memset(&estado, 0, sizeof(estado));

    /* El fifo tiene que existir ANTES de abrir el puerto: el ESP32 transmite
     * sin parar y receive_buf() puede dispararse apenas se abre. */
    ret = kfifo_alloc(&rx_fifo, FIFO_SIZE, GFP_KERNEL);
    if (ret)
        return ret;

    serdev_device_set_client_ops(serdev, &esp32_serdev_ops);

    ret = serdev_device_open(serdev);
    if (ret)
        goto err_fifo;

    of_property_read_u32(serdev->dev.of_node, "current-speed", &baudrate);
    serdev_device_set_baudrate(serdev, baudrate);
    serdev_device_set_flow_control(serdev, false);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "esp32_link: no se pudo reservar major/minor\n");
        goto err_close;
    }

    cdev_init(&esp32_cdev, &esp32_fops);
    esp32_cdev.owner = THIS_MODULE;
    ret = cdev_add(&esp32_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "esp32_link: no se pudo registrar el cdev\n");
        goto err_chrdev;
    }

    esp32_class = class_create(CLASS_NAME);
    if (IS_ERR(esp32_class)) {
        printk(KERN_ERR "esp32_link: no se pudo crear la clase\n");
        ret = PTR_ERR(esp32_class);
        goto err_cdev;
    }

    esp32_device = device_create(esp32_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(esp32_device)) {
        printk(KERN_ERR "esp32_link: no se pudo crear el dispositivo\n");
        ret = PTR_ERR(esp32_device);
        goto err_class;
    }

    dev_info(&serdev->dev, "esp32_link: listo, baudrate=%u, /dev/%s\n",
             baudrate, DEVICE_NAME);
    return 0;

err_class:
    class_destroy(esp32_class);
err_cdev:
    cdev_del(&esp32_cdev);
err_chrdev:
    unregister_chrdev_region(dev_num, 1);
err_close:
    serdev_device_close(serdev);
err_fifo:
    kfifo_free(&rx_fifo);
    return ret;
}

static void esp32_remove(struct serdev_device *serdev)
{
    device_destroy(esp32_class, dev_num);
    class_destroy(esp32_class);
    cdev_del(&esp32_cdev);
    unregister_chrdev_region(dev_num, 1);

    /* Cerrar el puerto ANTES de liberar el fifo: asi receive_buf() ya no
     * puede escribir en memoria liberada. */
    serdev_device_close(serdev);
    kfifo_free(&rx_fifo);

    dev_info(&serdev->dev, "esp32_link: modulo descargado\n");
}

static const struct of_device_id esp32_of_match[] = {
    { .compatible = "td3,esp32-link" },
    { }
};
MODULE_DEVICE_TABLE(of, esp32_of_match);

static struct serdev_device_driver esp32_driver = {
    .probe  = esp32_probe,
    .remove = esp32_remove,
    .driver = {
        .name           = "esp32_link",
        .of_match_table = esp32_of_match,
    },
};
module_serdev_device_driver(esp32_driver);