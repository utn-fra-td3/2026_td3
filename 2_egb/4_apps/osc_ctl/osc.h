#ifndef _OSC_H_
#define _OSC_H_

#include <linux/ioctl.h>

#define OSC_VAR_NAME_MAX 16

struct osc_var {
    char nombre[OSC_VAR_NAME_MAX];  /* "escala", "base", "flanco", ...  */
    int  valor;                     /* entrada en SET, salida en GET/SET */
};

#define OSC_MAGIC 'O'

#define OSC_SET _IOWR(OSC_MAGIC, 1, struct osc_var)
#define OSC_GET _IOWR(OSC_MAGIC, 2, struct osc_var)

#endif
