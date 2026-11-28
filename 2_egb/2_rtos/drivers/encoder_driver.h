#ifndef ENCODER_DRIVER_H
#define ENCODER_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// Inicializa el hardware del encoder (PCNT para giro y GPIO para switch)
void encoder_init(void);


#endif