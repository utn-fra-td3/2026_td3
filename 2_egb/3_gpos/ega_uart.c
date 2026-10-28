#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/poll.h>
#include <linux/serdev.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define DRIVER_NAME "ega_uart"
#define DEVICE_NAME "ega_uart"
#define RX_FIFO_SIZE 1024
#define IO_BUFFER_SIZE 256
#define UART_BAUDRATE 115200

static dev_t ega_dev_number;
static struct cdev ega_cdev;
static struct class *ega_class;
static struct serdev_device *ega_serdev;

static DEFINE_KFIFO(rx_fifo, unsigned char, RX_FIFO_SIZE);
static DEFINE_SPINLOCK(rx_lock);
static DEFINE_MUTEX(tx_lock);
static DECLARE_WAIT_QUEUE_HEAD(rx_wait_queue);
static atomic_t device_opened = ATOMIC_INIT(0);

static size_t ega_uart_receive(struct serdev_device *serdev,
                               const unsigned char *data, size_t size)
{
    unsigned int copied;

    copied = kfifo_in_spinlocked(&rx_fifo, data, size, &rx_lock);
    if (copied < size)
        dev_warn_ratelimited(&serdev->dev,
                             "buffer RX lleno, se descartaron %zu bytes\n",
                             size - copied);
    if (copied > 0)
        wake_up_interruptible(&rx_wait_queue);

    return size;
}

static const struct serdev_device_ops ega_uart_ops = {
    .receive_buf = ega_uart_receive,
};

static int ega_open(struct inode *inode, struct file *file)
{
    int result;

    if (atomic_cmpxchg(&device_opened, 0, 1) != 0)
        return -EBUSY;
    if (!READ_ONCE(ega_serdev)) {
        atomic_set(&device_opened, 0);
        return -ENODEV;
    }

    spin_lock_irq(&rx_lock);
    kfifo_reset(&rx_fifo);
    spin_unlock_irq(&rx_lock);
    result = nonseekable_open(inode, file);
    if (result)
        atomic_set(&device_opened, 0);
    return result;
}

static int ega_release(struct inode *inode, struct file *file)
{
    atomic_set(&device_opened, 0);
    return 0;
}

static ssize_t ega_read(struct file *file, char __user *buffer, size_t size,
                        loff_t *offset)
{
    unsigned char local_buffer[IO_BUFFER_SIZE];
    unsigned int count;
    int result;

    if (size == 0)
        return 0;

    if (kfifo_is_empty(&rx_fifo)) {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;
        result = wait_event_interruptible(rx_wait_queue,
                !kfifo_is_empty(&rx_fifo) || !READ_ONCE(ega_serdev));
        if (result)
            return result;
        if (!READ_ONCE(ega_serdev) && kfifo_is_empty(&rx_fifo))
            return -ENODEV;
    }

    count = min_t(size_t, size, sizeof(local_buffer));
    spin_lock_irq(&rx_lock);
    count = kfifo_out(&rx_fifo, local_buffer, count);
    spin_unlock_irq(&rx_lock);

    if (copy_to_user(buffer, local_buffer, count))
        return -EFAULT;
    return count;
}

static ssize_t ega_write(struct file *file, const char __user *buffer,
                         size_t size, loff_t *offset)
{
    unsigned char local_buffer[IO_BUFFER_SIZE];
    struct serdev_device *serdev;
    size_t total = 0;
    int written;

    if (size == 0)
        return 0;
    if (size > sizeof(local_buffer))
        return -EMSGSIZE;
    if (copy_from_user(local_buffer, buffer, size))
        return -EFAULT;

    mutex_lock(&tx_lock);
    serdev = READ_ONCE(ega_serdev);
    if (!serdev) {
        mutex_unlock(&tx_lock);
        return -ENODEV;
    }

    while (total < size) {
        written = serdev_device_write_buf(serdev, local_buffer + total,
                                          size - total);
        if (written < 0) {
            mutex_unlock(&tx_lock);
            return written;
        }
        if (written == 0) {
            mutex_unlock(&tx_lock);
            return total > 0 ? (ssize_t)total : -EAGAIN;
        }
        total += written;
    }
    serdev_device_wait_until_sent(serdev, 0);
    mutex_unlock(&tx_lock);
    return total;
}

static __poll_t ega_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &rx_wait_queue, wait);
    if (!kfifo_is_empty(&rx_fifo))
        mask |= EPOLLIN | EPOLLRDNORM;
    if (READ_ONCE(ega_serdev))
        mask |= EPOLLOUT | EPOLLWRNORM;
    else
        mask |= EPOLLERR | EPOLLHUP;
    return mask;
}

static const struct file_operations ega_file_operations = {
    .owner = THIS_MODULE,
    .open = ega_open,
    .release = ega_release,
    .read = ega_read,
    .write = ega_write,
    .poll = ega_poll,
};

static int ega_uart_probe(struct serdev_device *serdev)
{
    int result;

    serdev_device_set_client_ops(serdev, &ega_uart_ops);
    result = serdev_device_open(serdev);
    if (result)
        return dev_err_probe(&serdev->dev, result,
                             "no se pudo abrir el UART\n");

    serdev_device_set_baudrate(serdev, UART_BAUDRATE);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
    kfifo_reset(&rx_fifo);
    WRITE_ONCE(ega_serdev, serdev);
    dev_info(&serdev->dev, "EGA conectada a %d baudios 8N1\n",
             UART_BAUDRATE);
    return 0;
}

static void ega_uart_remove(struct serdev_device *serdev)
{
    mutex_lock(&tx_lock);
    WRITE_ONCE(ega_serdev, NULL);
    serdev_device_close(serdev);
    mutex_unlock(&tx_lock);
    wake_up_interruptible(&rx_wait_queue);
    dev_info(&serdev->dev, "EGA UART desconectada\n");
}

static const struct of_device_id ega_uart_of_match[] = {
    { .compatible = "brian,ega-uart" },
    { }
};
MODULE_DEVICE_TABLE(of, ega_uart_of_match);

static struct serdev_device_driver ega_uart_driver = {
    .probe = ega_uart_probe,
    .remove = ega_uart_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = ega_uart_of_match,
    },
};

static int __init ega_init(void)
{
    struct device *device;
    int result;

    result = alloc_chrdev_region(&ega_dev_number, 0, 1, DEVICE_NAME);
    if (result)
        return result;

    cdev_init(&ega_cdev, &ega_file_operations);
    ega_cdev.owner = THIS_MODULE;
    result = cdev_add(&ega_cdev, ega_dev_number, 1);
    if (result)
        goto error_unregister;

    ega_class = class_create(DRIVER_NAME);
    if (IS_ERR(ega_class)) {
        result = PTR_ERR(ega_class);
        goto error_cdev;
    }

    device = device_create(ega_class, NULL, ega_dev_number, NULL, DEVICE_NAME);
    if (IS_ERR(device)) {
        result = PTR_ERR(device);
        goto error_class;
    }

    result = serdev_device_driver_register(&ega_uart_driver);
    if (result)
        goto error_device;

    pr_info(DRIVER_NAME ": modulo cargado, char device /dev/%s\n",
            DEVICE_NAME);
    return 0;

error_device:
    device_destroy(ega_class, ega_dev_number);
error_class:
    class_destroy(ega_class);
error_cdev:
    cdev_del(&ega_cdev);
error_unregister:
    unregister_chrdev_region(ega_dev_number, 1);
    return result;
}

static void __exit ega_exit(void)
{
    serdev_device_driver_unregister(&ega_uart_driver);
    device_destroy(ega_class, ega_dev_number);
    class_destroy(ega_class);
    cdev_del(&ega_cdev);
    unregister_chrdev_region(ega_dev_number, 1);
    pr_info(DRIVER_NAME ": modulo descargado\n");
}

module_init(ega_init);
module_exit(ega_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brian Benavidez");
MODULE_DESCRIPTION("Driver UART y char device para la fuente partida EGA");
