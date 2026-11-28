#ifndef DDS_IOCTL_H
#define DDS_IOCTL_H

#include <linux/ioctl.h>

/* Número mágico para el driver */
#define DDS_MAGIC 'd'

/* Estructura para sincronización instantánea de estado */
struct dds_status_t {
    int freq_hz;
    int current_wave;
    int freq_step;
};

/* Comando IOCTL para leer el estado consolidado (Read from driver) */
#define DDS_IOCTL_GET_STATE _IOR(DDS_MAGIC, 1, struct dds_status_t)

#endif // DDS_IOCTL_H
