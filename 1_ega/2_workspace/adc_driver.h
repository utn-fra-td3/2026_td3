#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include <stdint.h>

// Inicializa los dos canales ADC (ADC1_CH6 = GPIO34, ADC1_CH7 = GPIO35)
void adc_driver_init(void);

// Devuelve el voltaje leído en el pin indicado, en mV (0–3300)
// canal: 6 o 7
uint32_t adc_read_mv(uint8_t canal);

#endif // ADC_DRIVER_H