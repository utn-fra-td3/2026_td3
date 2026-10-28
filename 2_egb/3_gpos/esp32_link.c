/*
 * esp32_link.c -- Driver de kernel para hablar con el ESP32 por UART.
 *
 * Arquitectura (de abajo hacia arriba):
 *   ESP32 <--UART--> serdev (driver de la UART en el kernel)
 *                      |
 *                  este modulo  <--->  /dev/esp32link (char device)
 *                                          |
 *                                  programa de usuario (eq_ctl)
 *
 * El Device Tree overlay declara un nodo "td3,esp32-link" colgado de la UART;
 * cuando el kernel lo encuentra, llama a esp32_probe() de este modulo.
 * Desde usuario se usa:
 *   - write()  : manda texto crudo al ESP32 (ej: "exec rock")
 *   - read()   : bloquea hasta que el ESP32 manda una linea
 *   - ioctl()  : ESP32_SET / ESP32_GET para fijar y consultar variables
 */

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
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/string.h>

#include "esp32_link.h"

#define DEVICE_NAME "esp32link"   /* aparece como /dev/esp32link */
#define CLASS_NAME  "td3"         /* clase en /sys/class/ */
#define MAX_LINE    64            /* largo maximo de una linea del protocolo */
#define FIFO_SIZE   256           /* bytes del kfifo de recepcion */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TD3 -- UTN FRA");
MODULE_DESCRIPTION("Char device sobre serdev: protocolo set/get/exec con un ESP32-S3 por UART");

/* Estado global del driver (hay un solo ESP32, por eso es global). */
static dev_t           dev_num;        /* major/minor reservado */
static struct cdev     esp32_cdev;     /* char device registrado en el kernel */
static struct class   *esp32_class;    /* clase para que udev cree /dev/esp32link */
static struct device  *esp32_device;   /* nodo de dispositivo */
static struct serdev_device *esp32_serdev; /* handle de la UART (serdev) */

/* read() como productor-consumidor: receive_buf() (productor) empuja cada
 * linea completa acá; esp32_read() (consumidor) se bloquea si esta vacio. */
static struct kfifo rx_fifo;
static DECLARE_WAIT_QUEUE_HEAD(rx_wait);

/* Ensamblado de bytes entrantes hasta encontrar un '\n'. */
static char   linebuf[MAX_LINE];
static size_t linelen;

/* Sincronismo dedicado para ioctl(ESP32_GET, ...): la misma linea que se
 * empuja siempre al kfifo de arriba, si hay un "get" pendiente, tambien se
 * copia acá y despierta a quien esta esperando esa respuesta puntual. */
static struct completion get_done;
static DEFINE_MUTEX(get_lock);
static char get_response[MAX_LINE];
static bool esperando_get;

/* ===================== serdev: recepcion ===================== */

/* El kernel llama a esta funcion cada vez que la UART recibe bytes.
 * Se arma una linea byte a byte en linebuf; al llegar '\n' la linea
 * completa se publica: va al kfifo (para read()) y, si hay un ioctl GET
 * esperando, tambien a get_response para despertarlo. */

static size_t esp32_receive_buf(struct serdev_device *serdev,
                                 const unsigned char *data, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        /* Acumular el byte (si la linea se pasa del maximo, se descartan los extra) */
        if (linelen < sizeof(linebuf) - 1)
            linebuf[linelen++] = data[i];

        if (data[i] == '\n') {
            /* Linea completa: entregarla a read() y despertar al lector */
            kfifo_in(&rx_fifo, linebuf, linelen);
            wake_up_interruptible(&rx_wait);

            /* Si un ioctl GET esta esperando respuesta, pasarle una copia */
            if (esperando_get) {
                linebuf[linelen] = '\0';
                strscpy(get_response, linebuf, sizeof(get_response));
                complete(&get_done);
            }

            linelen = 0; /* empezar a armar la proxima linea */
        }
    }

    return count; /* consumimos todo lo que nos dieron */
}

/* serdev_device_write() exige un write_wakeup registrado para aceptar un
 * timeout != 0 -- lo usa para saber cuando se libero espacio en el buffer
 * de transmision mientras espera. Sin el, devuelve -EINVAL de entrada. No
 * necesitamos hacer nada especial acá, alcanza con que exista. */
static void esp32_write_wakeup(struct serdev_device *serdev)
{
}

static const struct serdev_device_ops esp32_serdev_ops = {
    .receive_buf  = esp32_receive_buf,
    .write_wakeup = esp32_write_wakeup,
};

/* ===================== file_operations ===================== */

/* open/release no necesitan hacer nada: toda la inicializacion se hace en probe(). */

static int esp32_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int esp32_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* read(): entrega al usuario lo acumulado en el kfifo. Si esta vacio, el
 * proceso se duerme hasta que receive_buf() complete una linea (o devuelve
 * -EAGAIN si el archivo se abrio con O_NONBLOCK). */
static ssize_t esp32_read(struct file *file, char __user *buf,
                           size_t len, loff_t *off)
{
    unsigned int copiados;
    int ret;

    if (kfifo_is_empty(&rx_fifo)) {
        /* Sin datos: no bloquear si se pidio modo no bloqueante */
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(rx_wait, !kfifo_is_empty(&rx_fifo)))
            return -ERESTARTSYS;
    }

    /* Copiar del kfifo al buffer del usuario (hasta len bytes) */
    ret = kfifo_to_user(&rx_fifo, buf, len, &copiados);
    if (ret)
        return ret;

    return copiados;
}

/* write(): copia el texto del usuario a un buffer local, se asegura de que
 * termine en '\n' (el firmware trabaja por lineas) y lo manda por la UART. */
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

    /* Enviar por la UART; espera hasta 1 s si el buffer de transmision esta lleno */
    ret = serdev_device_write(esp32_serdev, local, n, msecs_to_jiffies(1000));
    printk(KERN_INFO "esp32_link: write() pidio %zu bytes, serdev_device_write devolvio %d\n",
           n, ret);
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

/* ioctl GET: manda "get VARIABLE\n" al ESP32 y bloquea hasta que
 * receive_buf() entregue la respuesta ("VARIABLE=valor"), con timeout de
 * 500 ms. Luego extrae el numero y lo devuelve al usuario en la estructura.
 * get_lock evita que dos GET simultaneos se pisen la respuesta. */
static long esp32_ioctl_get(struct esp32_var __user *argp)
{
    struct esp32_var var;
    char  msg[48];
    char *igual;
    long  ret;

    if (copy_from_user(&var, argp, sizeof(var)))
        return -EFAULT;
    var.nombre[sizeof(var.nombre) - 1] = '\0';

    mutex_lock(&get_lock);
    reinit_completion(&get_done);   /* descartar cualquier respuesta vieja */
    esperando_get = true;           /* avisar a receive_buf() que guarde la proxima linea */

    /* Armar y enviar el pedido */
    snprintf(msg, sizeof(msg), "get %s\n", var.nombre);
    serdev_device_write(esp32_serdev, msg, strlen(msg), msecs_to_jiffies(100));

    /* Dormir hasta que llegue la respuesta, se agote el tiempo o llegue una senal */
    ret = wait_for_completion_interruptible_timeout(&get_done,
                                                     msecs_to_jiffies(500));
    esperando_get = false;

    if (ret == 0) {
        mutex_unlock(&get_lock);
        return -ETIMEDOUT; /* el ESP32-S3 no contesto a tiempo */
    }
    if (ret < 0) {
        mutex_unlock(&get_lock);
        return ret; /* -ERESTARTSYS: la llamada se interrumpio */
    }

    /* La respuesta tiene la forma "nombre=valor": buscar el '=' y convertir lo que sigue */
    igual = strchr(get_response, '=');
    if (!igual || kstrtoint(igual + 1, 10, &var.valor)) {
        mutex_unlock(&get_lock);
        return -EPROTO; /* la respuesta no tenia el formato esperado */
    }
    mutex_unlock(&get_lock);

    /* Devolver la estructura con el valor ya cargado */
    return copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
}

/* Despachador de ioctl: elige la accion segun el comando recibido. */

static long esp32_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct esp32_var __user *argp = (struct esp32_var __user *)arg;
    struct esp32_var var;
    char msg[48];

    switch (cmd) {
    case ESP32_SET:
        /* SET: traer la estructura de usuario, armar "set VARIABLE VALOR\n"
         * y mandarlo por la UART (no espera respuesta). */
        if (copy_from_user(&var, argp, sizeof(var)))
            return -EFAULT;
        var.nombre[sizeof(var.nombre) - 1] = '\0';
        snprintf(msg, sizeof(msg), "set %s %d\n", var.nombre, var.valor);
        serdev_device_write(esp32_serdev, msg, strlen(msg), msecs_to_jiffies(100));
        return 0;

    case ESP32_GET:
        /* GET: se resuelve en la funcion de arriba porque necesita esperar la respuesta */
        return esp32_ioctl_get(argp);

    default:
        return -ENOTTY; /* comando no reconocido: convencion POSIX */
    }
}

/* Tabla que conecta las syscalls de usuario con las funciones del driver */
static const struct file_operations esp32_fops = {
    .owner          = THIS_MODULE,
    .open           = esp32_open,
    .release        = esp32_release,
    .read           = esp32_read,
    .write          = esp32_write,
    .unlocked_ioctl = esp32_ioctl,
};

/* ===================== serdev: probe/remove ===================== */

/* probe(): el kernel la llama cuando encuentra en el Device Tree el nodo
 * compatible con este driver. Configura la UART y crea /dev/esp32link.
 * Si algo falla, los labels err_* deshacen en orden inverso lo ya hecho. */

static int esp32_probe(struct serdev_device *serdev)
{
    u32 baudrate = 115200; /* valor por defecto si el DT no trae current-speed */
    int ret;

    esp32_serdev = serdev;
    /* Registrar los callbacks de recepcion/escritura de la UART */
    serdev_device_set_client_ops(serdev, &esp32_serdev_ops);

    /* Abrir el puerto serie */
    ret = serdev_device_open(serdev);
    if (ret)
        return ret;

    /* Velocidad: la del Device Tree (current-speed) o 115200 si no esta */
    of_property_read_u32(serdev->dev.of_node, "current-speed", &baudrate);
    serdev_device_set_baudrate(serdev, baudrate);
    serdev_device_set_flow_control(serdev, false); /* sin control de flujo por hardware */

    /* Buffer de recepcion para read() */
    ret = kfifo_alloc(&rx_fifo, FIFO_SIZE, GFP_KERNEL);
    if (ret)
        goto err_close;

    init_completion(&get_done);

    /* Reservar un major/minor dinamico para el char device */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "esp32_link: no se pudo reservar major/minor\n");
        goto err_fifo;
    }

    /* Asociar las file_operations al cdev y registrarlo */
    cdev_init(&esp32_cdev, &esp32_fops);
    esp32_cdev.owner = THIS_MODULE;
    ret = cdev_add(&esp32_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "esp32_link: no se pudo registrar el cdev\n");
        goto err_chrdev;
    }

    /* Crear la clase y el dispositivo: udev genera /dev/esp32link */
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
err_fifo:
    kfifo_free(&rx_fifo);
err_close:
    serdev_device_close(serdev);
    return ret;
}

/* remove(): libera todo en orden inverso al de probe(). */
static void esp32_remove(struct serdev_device *serdev)
{
    device_destroy(esp32_class, dev_num);
    class_destroy(esp32_class);
    cdev_del(&esp32_cdev);
    unregister_chrdev_region(dev_num, 1);
    kfifo_free(&rx_fifo);
    serdev_device_close(serdev);
    dev_info(&serdev->dev, "esp32_link: modulo descargado\n");
}

/* Tabla de matching con el Device Tree: debe coincidir con el "compatible"
 * del nodo esp32-link en el overlay (.dts). */
static const struct of_device_id esp32_of_match[] = {
    { .compatible = "td3,esp32-link" },
    { }
};
MODULE_DEVICE_TABLE(of, esp32_of_match);

/* Definicion del driver serdev y registro del modulo (reemplaza a
 * module_init/module_exit). */
static struct serdev_device_driver esp32_driver = {
    .probe  = esp32_probe,
    .remove = esp32_remove,
    .driver = {
        .name           = "esp32_link",
        .of_match_table = esp32_of_match,
    },
};
module_serdev_device_driver(esp32_driver);
