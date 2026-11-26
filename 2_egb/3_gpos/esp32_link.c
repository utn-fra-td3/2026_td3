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
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/string.h>

#include "esp32_link.h"

#define DEVICE_NAME "esp32link"
#define DEVICE_NAME_CMD "esp32link_cmd"
#define DEVICE_NAME_DATA "esp32link_data"
#define CLASS_NAME "td3"

/* Un nodo por minor: comandos/respuestas y puntos del barrido */
#define MINOR_CMD 0
#define MINOR_DATA 1
#define CANT_NODOS 2

#define FIFO_CMD_SIZE 128
#define FIFO_DATA_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lapietra - Monffyllo");
MODULE_DESCRIPTION("Char device sobre serdev: comandos por /dev/esp32link_cmd y puntos del barrido por /dev/esp32link_data con un ESP32-S3 por UART");

static dev_t dev_num;
static struct cdev esp32_cdev;
static struct class *esp32_class;
static struct device *esp32_device;
static struct serdev_device *esp32_serdev;

/* read() como productor-consumidor: receive_buf() (productor) empuja cada
 * linea completa al fifo del nodo que corresponde; esp32_read() (consumidor)
 * se bloquea en la wait_queue de ese nodo si su fifo esta vacio.
 * Un nodo por minor: nodos[MINOR_CMD] y nodos[MINOR_DATA]. */
struct esp32_nodo
{
    struct kfifo rx_fifo;
    wait_queue_head_t rx_wait;
};
static struct esp32_nodo nodos[CANT_NODOS];

/* Ensamblado de bytes entrantes hasta encontrar un '\n'. */
static char linebuf[MAX_LINE];
static size_t linelen;

/* ===================== serdev: recepcion ===================== */

static size_t esp32_receive_buf(struct serdev_device *serdev, const unsigned char *data, size_t count)
{
    unsigned int minor;
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (linelen < sizeof(linebuf) - 1)
            linebuf[linelen++] = data[i];

        if (data[i] == '\n')
        {
            if (linelen > 1)
            {
                /* Ruteo por prefijo: el encabezado del barrido (SWEEP) y
                 * sus puntos (POINT) van a data, cualquier otra linea
                 * (OK, ERR, respuestas a get) a cmd. */
                if (linelen >= 5 && (strncmp(linebuf, "SWEEP", 5) == 0 ||
                                     strncmp(linebuf, "POINT", 5) == 0))
                    minor = MINOR_DATA;
                else
                    minor = MINOR_CMD;

                /* Se encola la linea entera o nada: nunca media linea */
                if (kfifo_avail(&nodos[minor].rx_fifo) >= linelen)
                {
                    kfifo_in(&nodos[minor].rx_fifo, linebuf, linelen);
                    wake_up_interruptible(&nodos[minor].rx_wait);
                }
                else
                {
                    printk_ratelimited(KERN_WARNING "esp32_link: fifo lleno, se descarta una linea\n");
                }
            }
            linelen = 0;
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
    .receive_buf = esp32_receive_buf,
    .write_wakeup = esp32_write_wakeup,
};

/* ===================== file_operations ===================== */

static int esp32_open(struct inode *inode, struct file *file)
{
    /* data es solo lectura: no se puede abrir para escribir */
    if (iminor(inode) == MINOR_DATA && (file->f_mode & FMODE_WRITE))
        return -EPERM;
    return 0;
}

static int esp32_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t esp32_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
    unsigned int minor = iminor(file_inode(file)); // nodo que se esta leyendo
    unsigned int copiados;
    int ret;

    if (kfifo_is_empty(&nodos[minor].rx_fifo))
    {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        if (wait_event_interruptible(nodos[minor].rx_wait, !kfifo_is_empty(&nodos[minor].rx_fifo)))
            return -ERESTARTSYS;
    }

    ret = kfifo_to_user(&nodos[minor].rx_fifo, buf, len, &copiados);
    if (ret)
        return ret;

    return copiados;
}

/* poll()/select() desde usuario: se registra en la misma wait_queue que
 * despierta receive_buf(), asi el proceso se despierta cuando llega una
 * linea. Hay datos para leer si el fifo del nodo no esta vacio */
static __poll_t esp32_poll(struct file *file, poll_table *wait)
{
    unsigned int minor = iminor(file_inode(file));
    __poll_t mask = 0;

    poll_wait(file, &nodos[minor].rx_wait, wait);

    if (!kfifo_is_empty(&nodos[minor].rx_fifo))
        mask |= EPOLLIN;
    return mask;
}

static ssize_t esp32_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    char local[MAX_LINE];
    size_t n = len;
    int ret;

    if (n >= sizeof(local))
        n = sizeof(local) - 1; /* deja lugar para agregar el '\n' */

    if (copy_from_user(local, buf, n))
        return -EFAULT;

    /* Asegurar el delimitador de linea que espera el firmware */
    if (n == 0 || local[n - 1] != '\n')
        local[n++] = '\n';

    ret = serdev_device_write(esp32_serdev, local, n, msecs_to_jiffies(1000));
    printk(KERN_INFO "esp32_link: write() pidio %zu bytes, serdev_device_write devolvio %d\n", n, ret);
    if (ret < 0)
    {
        printk(KERN_ERR "esp32_link: serdev_device_write() fallo en write() (%d)\n", ret);
        return ret;
    }
    if (ret != n)
    {
        printk(KERN_WARNING "esp32_link: write parcial, se mandaron %d de %zu bytes\n", ret, n);
    }

    return len; /* se consumio todo el buffer original */
}

static const struct file_operations esp32_fops = {
    .owner = THIS_MODULE,
    .open = esp32_open,
    .release = esp32_release,
    .read = esp32_read,
    .write = esp32_write,
    .poll = esp32_poll,
};

/* Permisos de los nodos al crearlos: cmd lectura/escritura para todos, data solo lectura */
static char *esp32_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = (MINOR(dev->devt) == MINOR_DATA) ? 0444 : 0666;
    return NULL;
}

/* ===================== serdev: probe/remove ===================== */

static int esp32_probe(struct serdev_device *serdev)
{
    u32 baudrate = 115200; /* valor por defecto si el DT no trae current-speed */
    int ret;

    esp32_serdev = serdev;
    serdev_device_set_client_ops(serdev, &esp32_serdev_ops);

    /* Un fifo y una wait_queue por nodo */
    init_waitqueue_head(&nodos[MINOR_CMD].rx_wait);
    init_waitqueue_head(&nodos[MINOR_DATA].rx_wait);

    ret = kfifo_alloc(&nodos[MINOR_CMD].rx_fifo, FIFO_CMD_SIZE, GFP_KERNEL);
    if (ret)
        return ret;

    ret = kfifo_alloc(&nodos[MINOR_DATA].rx_fifo, FIFO_DATA_SIZE, GFP_KERNEL);
    if (ret)
        goto err_fifo_cmd;

    ret = serdev_device_open(serdev);
    if (ret)
        goto err_fifo_data;

    of_property_read_u32(serdev->dev.of_node, "current-speed", &baudrate);
    serdev_device_set_baudrate(serdev, baudrate);
    serdev_device_set_flow_control(serdev, false);

    /* Un major con dos minors: 0 = cmd, 1 = data */
    ret = alloc_chrdev_region(&dev_num, 0, CANT_NODOS, DEVICE_NAME);
    if (ret < 0)
    {
        printk(KERN_ERR "esp32_link: no se pudo reservar major/minor\n");
        goto err_close;
    }

    cdev_init(&esp32_cdev, &esp32_fops);
    esp32_cdev.owner = THIS_MODULE;
    ret = cdev_add(&esp32_cdev, dev_num, CANT_NODOS);
    if (ret < 0)
    {
        printk(KERN_ERR "esp32_link: no se pudo registrar el cdev\n");
        goto err_chrdev;
    }

    esp32_class = class_create(CLASS_NAME);
    if (IS_ERR(esp32_class))
    {
        printk(KERN_ERR "esp32_link: no se pudo crear la clase\n");
        ret = PTR_ERR(esp32_class);
        goto err_cdev;
    }
    esp32_class->devnode = esp32_devnode; /* antes de device_create */

    esp32_device = device_create(esp32_class, NULL, MKDEV(MAJOR(dev_num), MINOR_CMD), NULL, DEVICE_NAME_CMD);
    if (IS_ERR(esp32_device))
    {
        printk(KERN_ERR "esp32_link: no se pudo crear el dispositivo %s\n", DEVICE_NAME_CMD);
        ret = PTR_ERR(esp32_device);
        goto err_class;
    }

    esp32_device = device_create(esp32_class, NULL, MKDEV(MAJOR(dev_num), MINOR_DATA), NULL, DEVICE_NAME_DATA);
    if (IS_ERR(esp32_device))
    {
        printk(KERN_ERR "esp32_link: no se pudo crear el dispositivo %s\n", DEVICE_NAME_DATA);
        ret = PTR_ERR(esp32_device);
        goto err_device_cmd;
    }

    dev_info(&serdev->dev, "esp32_link: listo, baudrate=%u, major=%d, /dev/%s y /dev/%s\n",
             baudrate, MAJOR(dev_num), DEVICE_NAME_CMD, DEVICE_NAME_DATA);
    return 0;

err_device_cmd:
    device_destroy(esp32_class, MKDEV(MAJOR(dev_num), MINOR_CMD));
err_class:
    class_destroy(esp32_class);
err_cdev:
    cdev_del(&esp32_cdev);
err_chrdev:
    unregister_chrdev_region(dev_num, CANT_NODOS);
err_close:
    serdev_device_close(serdev); /* primero cerrar: deja de llamarse receive_buf() */
err_fifo_data:
    kfifo_free(&nodos[MINOR_DATA].rx_fifo);
err_fifo_cmd:
    kfifo_free(&nodos[MINOR_CMD].rx_fifo);
    return ret;
}

static void esp32_remove(struct serdev_device *serdev)
{
    device_destroy(esp32_class, MKDEV(MAJOR(dev_num), MINOR_DATA));
    device_destroy(esp32_class, MKDEV(MAJOR(dev_num), MINOR_CMD));
    class_destroy(esp32_class);
    cdev_del(&esp32_cdev);
    unregister_chrdev_region(dev_num, CANT_NODOS);
    /* Cerrar el puerto antes de liberar los fifos: despues de esto ya no
     * se llama a receive_buf(), que es la que escribe en ellos. */
    serdev_device_close(serdev);
    kfifo_free(&nodos[MINOR_DATA].rx_fifo);
    kfifo_free(&nodos[MINOR_CMD].rx_fifo);
    dev_info(&serdev->dev, "esp32_link: modulo descargado\n");
}

static const struct of_device_id esp32_of_match[] = {
    {.compatible = "td3,esp32-link"},
    {}};
MODULE_DEVICE_TABLE(of, esp32_of_match);

static struct serdev_device_driver esp32_driver = {
    .probe = esp32_probe,
    .remove = esp32_remove,
    .driver = {
        .name = "esp32_link",
        .of_match_table = esp32_of_match,
    },
};
module_serdev_device_driver(esp32_driver);
