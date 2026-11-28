#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/file.h>
#include "dds_ioctl.h"

#define DEVICE_NAME "generador_dds"
#define CLASS_NAME  "dds_class"
#define UART_PATH   "/dev/ttyAMA0"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TD3 - UTN FRA");
MODULE_DESCRIPTION("Driver Char Device para DDS con UART Persistente e IOCTL");
MODULE_VERSION("2.1");

static int major;
static struct class*  dds_class  = NULL;
static struct device* dds_device = NULL;

static struct dds_status_t g_driver_state = {
    .freq_hz = 1000,
    .current_wave = 0,
    .freq_step = 100
};

/* dds_open: abre el puerto físico una sola vez y asocia el descriptor */
static int dds_open(struct inode *inodep, struct file *filep) {
    struct file *uart_filp = filp_open(UART_PATH, O_RDWR | O_NOCTTY | O_NONBLOCK, 0);
    if (IS_ERR(uart_filp)) {
        pr_err("Error abriendo %s en dds_open: %ld\n", UART_PATH, PTR_ERR(uart_filp));
        return PTR_ERR(uart_filp);
    }
    filep->private_data = uart_filp;
    return 0;
}

/* dds_release: cierra el descriptor cuando el proceso termina */
static int dds_release(struct inode *inodep, struct file *filep) {
    if (filep->private_data) {
        filp_close((struct file *)filep->private_data, NULL);
        filep->private_data = NULL;
    }
    return 0;
}

static ssize_t dds_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    struct file *uart_filp = (struct file *)filep->private_data;
    char kbuf[128];
    ssize_t bytes_to_write;

    if (!uart_filp) return -EBADF;
    if (len == 0) return 0;

    bytes_to_write = min(len, sizeof(kbuf) - 1);
    if (copy_from_user(kbuf, buffer, bytes_to_write)) {
        return -EFAULT;
    }
    kbuf[bytes_to_write] = '\0';

    return kernel_write(uart_filp, kbuf, bytes_to_write, &uart_filp->f_pos);
}

static ssize_t dds_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset) {
    struct file *uart_filp = (struct file *)filep->private_data;
    char kbuf[256];
    ssize_t bytes_read;

    if (!uart_filp) return -EBADF;

    bytes_read = kernel_read(uart_filp, kbuf, min(len, sizeof(kbuf) - 1), &uart_filp->f_pos);

    if (bytes_read > 0) {
        kbuf[bytes_read] = '\0';

        /* Parseo en Kernel para actualizar el estado que consulta IOCTL */
        if (strstr(kbuf, "STATUS:FREQ=")) {
            sscanf(strstr(kbuf, "STATUS:FREQ="), "STATUS:FREQ=%d", &g_driver_state.freq_hz);
        } else if (strstr(kbuf, "STATUS:WAVE=")) {
            sscanf(strstr(kbuf, "STATUS:WAVE="), "STATUS:WAVE=%d", &g_driver_state.current_wave);
        } else if (strstr(kbuf, "STATUS:STEP=")) {
            sscanf(strstr(kbuf, "STATUS:STEP="), "STATUS:STEP=%d", &g_driver_state.freq_step);
        } else if (strstr(kbuf, "STATUS:NVS_SAVED:")) {
            sscanf(strstr(kbuf, "STATUS:NVS_SAVED:"), "STATUS:NVS_SAVED:%d,%d,%d",
                   &g_driver_state.freq_hz, &g_driver_state.current_wave, &g_driver_state.freq_step);
        }

        if (copy_to_user(buffer, kbuf, bytes_read)) {
            return -EFAULT;
        }
    }

    return bytes_read;
}

static long dds_unlocked_ioctl(struct file *filep, unsigned int cmd, unsigned long arg) {
    pr_info("DDS: ioctl invocado con cmd=0x%X\n", cmd);
    switch (cmd) {
        case DDS_IOCTL_GET_STATE:
            if (copy_to_user((struct dds_status_t __user *)arg, &g_driver_state, sizeof(struct dds_status_t))) {
                return -EFAULT;
            }
            break;
        default:
            pr_warn("DDS: ioctl cmd desconocido: 0x%X (esperado: 0x%lX)\n", cmd, (unsigned long)DDS_IOCTL_GET_STATE);
            return -ENOTTY;
    }
    return 0;
}

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = dds_open,
    .release        = dds_release,
    .read           = dds_read,
    .write          = dds_write,
    .unlocked_ioctl = dds_unlocked_ioctl,
};

static int __init dds_driver_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) return major;

    dds_class = class_create(CLASS_NAME);
    if (IS_ERR(dds_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(dds_class);
    }

    dds_device = device_create(dds_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(dds_device)) {
        class_destroy(dds_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(dds_device);
    }

    pr_info("DDS Driver v2.1 (Descriptor Persistente) cargado.\n");
    return 0;
}

static void __exit dds_driver_exit(void) {
    device_destroy(dds_class, MKDEV(major, 0));
    class_unregister(dds_class);
    class_destroy(dds_class);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("DDS Driver descargado.\n");
}

module_init(dds_driver_init);
module_exit(dds_driver_exit);
