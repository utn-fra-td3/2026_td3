
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
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/string.h>

#include "osc.h"

#define DEVICE_NAME   "osc"
#define CLASS_NAME    "osc"
#define MAX_LINE      64
#define FIFO_SIZE     16384        /* potencia de 2 (requisito de kfifo) */
#define CMD_TIMEOUT   1000         /* ms esperando respuesta del ESP     */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Del Pozo, Montoni -- TD3 UTN FRA");
MODULE_DESCRIPTION("Osciloscopio ESP32-S3: char device sobre serdev con protocolo get/set/exec");

static dev_t                 dev_num;
static struct cdev           osc_cdev;
static struct class         *osc_class;
static struct device        *osc_device;
static struct serdev_device *osc_serdev;

static struct kfifo rx_fifo;
static DECLARE_WAIT_QUEUE_HEAD(rx_wait);
static DEFINE_MUTEX(read_lock);
static atomic_t abiertos = ATOMIC_INIT(0);
static unsigned long lineas_perdidas;

static char   linebuf[MAX_LINE];
static size_t linelen;

static DEFINE_MUTEX(cmd_lock);        /* un comando a la vez                */
static DEFINE_SPINLOCK(pend_lock);    /* protege los 3 campos de abajo      */
static bool esperando;
static char pend_nombre[OSC_VAR_NAME_MAX];
static char pend_resp[MAX_LINE];
static struct completion cmd_done;


static bool es_respuesta(const char *linea, const char *nombre)
{
    size_t n = strlen(nombre);

    if (!strncmp(linea, nombre, n) && linea[n] == '=')
        return true;
    if (!strncmp(linea, "err ", 4) && !strncmp(linea + 4, nombre, n) && linea[4 + n] == ' ')
        return true;
    return false;
}

static void linea_completa(void)
{
    unsigned long flags;

    if (atomic_read(&abiertos) > 0) {
        if (kfifo_avail(&rx_fifo) >= linelen) {
            kfifo_in(&rx_fifo, linebuf, linelen);
            wake_up_interruptible(&rx_wait);
        } else {
            lineas_perdidas++;
        }
    }

    linebuf[linelen - 1] = '\0';            /* saca el '\n' para parsear */
    spin_lock_irqsave(&pend_lock, flags);
    if (esperando && es_respuesta(linebuf, pend_nombre)) {
        strscpy(pend_resp, linebuf, sizeof(pend_resp));
        esperando = false;
        complete(&cmd_done);
    }
    spin_unlock_irqrestore(&pend_lock, flags);
}

typedef typeof(((struct serdev_device_ops *)0)->receive_buf(NULL, NULL, 0)) rx_ret_t;

static rx_ret_t osc_receive_buf(struct serdev_device *serdev,
                                const unsigned char *data, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        char c = data[i];

        if (c == '\r')
            continue;

        if (linelen < sizeof(linebuf) - 1)
            linebuf[linelen++] = c;
        else if (c != '\n')
            continue;                       /* linea demasiado larga: se trunca */
        else
            linebuf[linelen - 1] = c;       /* garantiza el '\n' final          */

        if (c == '\n') {
            linea_completa();
            linelen = 0;
        }
    }
    return count;
}

static void osc_write_wakeup(struct serdev_device *serdev)
{
}

static const struct serdev_device_ops osc_serdev_ops = {
    .receive_buf  = osc_receive_buf,
    .write_wakeup = osc_write_wakeup,
};

static int error_de_protocolo(const char *resp, const char *nombre)
{
    int codigo;

    if (kstrtoint(resp + 4 + strlen(nombre) + 1, 10, &codigo))
        return -EPROTO;

    switch (codigo) {
    case 1:  return -EINVAL;   /* valor fuera de rango      */
    case 2:  return -EPERM;    /* variable de solo lectura  */
    case 3:  return -ENOENT;   /* variable desconocida      */
    case 4:  return -EIO;      /* el cambio no se aplico    */
    default: return -EPROTO;
    }
}

static long enviar_y_esperar(struct osc_var *var, const char *msg)
{
    unsigned long flags;
    char   resp[MAX_LINE];
    char  *igual;
    long   ret;

    mutex_lock(&cmd_lock);

    spin_lock_irqsave(&pend_lock, flags);
    strscpy(pend_nombre, var->nombre, sizeof(pend_nombre));
    reinit_completion(&cmd_done);
    esperando = true;
    spin_unlock_irqrestore(&pend_lock, flags);

    ret = serdev_device_write(osc_serdev, msg, strlen(msg), msecs_to_jiffies(100));
    if (ret < (long)strlen(msg)) {
        ret = (ret < 0) ? ret : -EIO;
        goto fin;
    }

    ret = wait_for_completion_interruptible_timeout(&cmd_done,
                                                    msecs_to_jiffies(CMD_TIMEOUT));
    if (ret == 0) {
        ret = -ETIMEDOUT;              /* el ESP no contesto a tiempo */
        goto fin;
    }
    if (ret < 0)
        goto fin;                      /* -ERESTARTSYS: interrumpido  */

    spin_lock_irqsave(&pend_lock, flags);
    strscpy(resp, pend_resp, sizeof(resp));
    spin_unlock_irqrestore(&pend_lock, flags);

    if (!strncmp(resp, "err ", 4)) {
        ret = error_de_protocolo(resp, var->nombre);
        goto fin;
    }

    igual = strchr(resp, '=');
    ret = (igual && !kstrtoint(igual + 1, 10, &var->valor)) ? 0 : -EPROTO;

fin:
    spin_lock_irqsave(&pend_lock, flags);
    esperando = false;
    spin_unlock_irqrestore(&pend_lock, flags);
    mutex_unlock(&cmd_lock);
    return ret;
}

static int osc_open(struct inode *inode, struct file *file)
{
    if (atomic_inc_return(&abiertos) == 1) {
        mutex_lock(&read_lock);
        kfifo_reset_out(&rx_fifo);
        mutex_unlock(&read_lock);
    }
    return 0;
}

static int osc_release(struct inode *inode, struct file *file)
{
    atomic_dec(&abiertos);
    return 0;
}

static ssize_t osc_read(struct file *file, char __user *buf,
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

    mutex_lock(&read_lock);
    ret = kfifo_to_user(&rx_fifo, buf, len, &copiados);
    mutex_unlock(&read_lock);

    return ret ? ret : copiados;
}

static ssize_t osc_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *off)
{
    char   local[MAX_LINE];
    size_t n = len;
    int    ret;

    if (n >= sizeof(local) - 1)
        n = sizeof(local) - 2;

    if (copy_from_user(local, buf, n))
        return -EFAULT;

    if (n == 0 || local[n - 1] != '\n')
        local[n++] = '\n';

    ret = serdev_device_write(osc_serdev, local, n, msecs_to_jiffies(1000));
    if (ret < 0)
        return ret;

    return len;
}

static long osc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct osc_var __user *argp = (struct osc_var __user *)arg;
    struct osc_var var;
    char   msg[48];
    long   ret;

    if (cmd != OSC_SET && cmd != OSC_GET)
        return -ENOTTY;                /* comando no reconocido: convencion POSIX */

    if (copy_from_user(&var, argp, sizeof(var)))
        return -EFAULT;
    var.nombre[sizeof(var.nombre) - 1] = '\0';

    if (cmd == OSC_SET)
        snprintf(msg, sizeof(msg), "set %s %d\n", var.nombre, var.valor);
    else
        snprintf(msg, sizeof(msg), "get %s\n", var.nombre);

    ret = enviar_y_esperar(&var, msg);
    if (ret)
        return ret;

    return copy_to_user(argp, &var, sizeof(var)) ? -EFAULT : 0;
}

static const struct file_operations osc_fops = {
    .owner          = THIS_MODULE,
    .open           = osc_open,
    .release        = osc_release,
    .read           = osc_read,
    .write          = osc_write,
    .unlocked_ioctl = osc_ioctl,
};

/* Permisos del nodo: rw para todos, asi las apps no necesitan sudo */
static char *osc_devnode(const struct device *dev, umode_t *mode)
{
    if (mode)
        *mode = 0666;
    return NULL;
}

static int osc_probe(struct serdev_device *serdev)
{
    u32 baudrate = 115200;   /* por defecto, si el DT no trae current-speed */
    int ret;

    osc_serdev = serdev;
    serdev_device_set_client_ops(serdev, &osc_serdev_ops);

    ret = serdev_device_open(serdev);
    if (ret)
        return ret;

    of_property_read_u32(serdev->dev.of_node, "current-speed", &baudrate);
    serdev_device_set_baudrate(serdev, baudrate);
    serdev_device_set_flow_control(serdev, false);

    ret = kfifo_alloc(&rx_fifo, FIFO_SIZE, GFP_KERNEL);
    if (ret)
        goto err_close;

    init_completion(&cmd_done);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        goto err_fifo;

    cdev_init(&osc_cdev, &osc_fops);
    osc_cdev.owner = THIS_MODULE;
    ret = cdev_add(&osc_cdev, dev_num, 1);
    if (ret < 0)
        goto err_chrdev;

    osc_class = class_create(CLASS_NAME);
    if (IS_ERR(osc_class)) {
        ret = PTR_ERR(osc_class);
        goto err_cdev;
    }
    osc_class->devnode = osc_devnode;

    osc_device = device_create(osc_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(osc_device)) {
        ret = PTR_ERR(osc_device);
        goto err_class;
    }

    dev_info(&serdev->dev, "osc: listo, %u baud, /dev/%s (major %d)\n",
             baudrate, DEVICE_NAME, MAJOR(dev_num));
    return 0;

err_class:
    class_destroy(osc_class);
err_cdev:
    cdev_del(&osc_cdev);
err_chrdev:
    unregister_chrdev_region(dev_num, 1);
err_fifo:
    kfifo_free(&rx_fifo);
err_close:
    serdev_device_close(serdev);
    dev_err(&serdev->dev, "osc: fallo el probe (%d)\n", ret);
    return ret;
}

static void osc_remove(struct serdev_device *serdev)
{
    device_destroy(osc_class, dev_num);
    class_destroy(osc_class);
    cdev_del(&osc_cdev);
    unregister_chrdev_region(dev_num, 1);
    serdev_device_close(serdev);
    kfifo_free(&rx_fifo);
    dev_info(&serdev->dev, "osc: descargado (lineas perdidas por kfifo lleno: %lu)\n",
             lineas_perdidas);
}

/* El string compatible es el que une este driver con el nodo del overlay */
static const struct of_device_id osc_of_match[] = {
    { .compatible = "utnfra,osc-esp32" },
    { }
};
MODULE_DEVICE_TABLE(of, osc_of_match);

static struct serdev_device_driver osc_driver = {
    .probe  = osc_probe,
    .remove = osc_remove,
    .driver = {
        .name           = "osc",
        .of_match_table = osc_of_match,
    },
};
module_serdev_device_driver(osc_driver);
