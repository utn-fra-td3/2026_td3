#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define DEVICE_NAME "egalink"
#define CLASS_NAME  "ega"
#define FIFO_SIZE   1024
#define TX_MAX      256

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gonzalez Jorja - TD3 UTN FRA");
MODULE_DESCRIPTION("Driver UART EGA-EGB mediante serdev");

static dev_t ega_dev_num;
static struct cdev ega_cdev;
static struct class *ega_class;
static struct device *ega_device;

static struct serdev_device *ega_serdev;

static struct kfifo rx_fifo;
static DECLARE_WAIT_QUEUE_HEAD(rx_wait);

static DEFINE_MUTEX(tx_mutex);


/* ============================================================
 * RECEPCION DESDE UART
 * ============================================================ */

static size_t ega_receive_buf(struct serdev_device *serdev,
                              const unsigned char *data,
                              size_t count)
{
    unsigned int inserted;

    inserted = kfifo_in(&rx_fifo, data, count);

    if (inserted > 0)
        wake_up_interruptible(&rx_wait);

    if (inserted < count)
        dev_warn(&serdev->dev,
                 "FIFO RX lleno: %zu bytes recibidos, %u almacenados\n",
                 count, inserted);

    return count;
}


static void ega_write_wakeup(struct serdev_device *serdev)
{
}


static const struct serdev_device_ops ega_serdev_ops = {
    .receive_buf  = ega_receive_buf,
    .write_wakeup = ega_write_wakeup,
};


/* ============================================================
 * CHAR DEVICE
 * ============================================================ */

static int ega_open(struct inode *inode, struct file *file)
{
    return 0;
}


static int ega_release(struct inode *inode, struct file *file)
{
    return 0;
}


static ssize_t ega_read(struct file *file,
                        char __user *buf,
                        size_t len,
                        loff_t *offset)
{
    unsigned int copied;
    int ret;

    if (kfifo_is_empty(&rx_fifo)) {

        ret = wait_event_interruptible(
            rx_wait,
            !kfifo_is_empty(&rx_fifo)
        );

        if (ret)
            return ret;
    }

    ret = kfifo_to_user(
        &rx_fifo,
        buf,
        len,
        &copied
    );

    if (ret)
        return ret;

    return copied;
}


static ssize_t ega_write(struct file *file,
                         const char __user *buf,
                         size_t len,
                         loff_t *offset)
{
    char local[TX_MAX];
    size_t n;
    int ret;

    if (!ega_serdev)
        return -ENODEV;

    if (len == 0)
        return 0;

    if (len >= TX_MAX)
        return -EMSGSIZE;

    if (copy_from_user(local, buf, len))
        return -EFAULT;

    n = len;

    /*
     * Nuestro protocolo UART trabaja por lineas.
     * Si user space no puso LF, lo agregamos.
     */
    if (local[n - 1] != '\n') {

        if (n >= TX_MAX - 1)
            return -EMSGSIZE;

        local[n++] = '\n';
    }

    mutex_lock(&tx_mutex);

    ret = serdev_device_write(
        ega_serdev,
        local,
        n,
        msecs_to_jiffies(1000)
    );

    mutex_unlock(&tx_mutex);

    if (ret < 0)
        return ret;

    if (ret != n) {
        dev_warn(&ega_serdev->dev,
                 "Escritura parcial UART: %d/%zu bytes\n",
                 ret, n);
    }

    /*
     * Desde el punto de vista de user space consumimos
     * el buffer original.
     */
    return len;
}


static const struct file_operations ega_fops = {
    .owner   = THIS_MODULE,
    .open    = ega_open,
    .release = ega_release,
    .read    = ega_read,
    .write   = ega_write,
};


/* ============================================================
 * SERDEV PROBE
 * ============================================================ */

static int ega_probe(struct serdev_device *serdev)
{
    int ret;
    u32 baudrate = 115200;

    dev_info(&serdev->dev, "EGA link detectado\n");

    ega_serdev = serdev;

    serdev_device_set_client_ops(
        serdev,
        &ega_serdev_ops
    );

    ret = serdev_device_open(serdev);

    if (ret) {
        dev_err(&serdev->dev,
                "No se pudo abrir UART: %d\n",
                ret);
        ega_serdev = NULL;
        return ret;
    }

    of_property_read_u32(
        serdev->dev.of_node,
        "current-speed",
        &baudrate
    );

    serdev_device_set_baudrate(
        serdev,
        baudrate
    );

    serdev_device_set_flow_control(
        serdev,
        false
    );

    ret = kfifo_alloc(
        &rx_fifo,
        FIFO_SIZE,
        GFP_KERNEL
    );

    if (ret)
        goto err_close;

    /*
     * Reservar major/minor dinámicamente.
     */
    ret = alloc_chrdev_region(
        &ega_dev_num,
        0,
        1,
        DEVICE_NAME
    );

    if (ret)
        goto err_fifo;

    /*
     * Registrar char device.
     */
    cdev_init(
        &ega_cdev,
        &ega_fops
    );

    ega_cdev.owner = THIS_MODULE;

    ret = cdev_add(
        &ega_cdev,
        ega_dev_num,
        1
    );

    if (ret)
        goto err_chrdev;

    /*
     * Crear clase visible en sysfs.
     */
    ega_class = class_create(CLASS_NAME);

    if (IS_ERR(ega_class)) {
        ret = PTR_ERR(ega_class);
        goto err_cdev;
    }

    /*
     * Esto hará aparecer /dev/egalink mediante udev.
     */
    ega_device = device_create(
        ega_class,
        NULL,
        ega_dev_num,
        NULL,
        DEVICE_NAME
    );

    if (IS_ERR(ega_device)) {
        ret = PTR_ERR(ega_device);
        goto err_class;
    }

    dev_info(
        &serdev->dev,
        "Driver listo: /dev/%s, baudrate=%u\n",
        DEVICE_NAME,
        baudrate
    );

    return 0;


err_class:

    class_destroy(ega_class);

err_cdev:

    cdev_del(&ega_cdev);

err_chrdev:

    unregister_chrdev_region(
        ega_dev_num,
        1
    );

err_fifo:

    kfifo_free(&rx_fifo);

err_close:

    serdev_device_close(serdev);
    ega_serdev = NULL;

    return ret;
}


/* ============================================================
 * REMOVE
 * ============================================================ */

static void ega_remove(struct serdev_device *serdev)
{
    device_destroy(
        ega_class,
        ega_dev_num
    );

    class_destroy(
        ega_class
    );

    cdev_del(
        &ega_cdev
    );

    unregister_chrdev_region(
        ega_dev_num,
        1
    );

    kfifo_free(
        &rx_fifo
    );

    serdev_device_close(
        serdev
    );

    ega_serdev = NULL;

    dev_info(
        &serdev->dev,
        "Driver EGA descargado\n"
    );
}


/* ============================================================
 * DEVICE TREE MATCH
 * ============================================================ */

static const struct of_device_id ega_of_match[] = {

    {
        .compatible = "td3,ega-link"
    },

    { }
};

MODULE_DEVICE_TABLE(of, ega_of_match);


/* ============================================================
 * SERDEV DRIVER
 * ============================================================ */

static struct serdev_device_driver ega_driver = {

    .probe  = ega_probe,
    .remove = ega_remove,

    .driver = {

        .name = "ega_link",

        .of_match_table = ega_of_match,
    },
};


module_serdev_device_driver(ega_driver);
