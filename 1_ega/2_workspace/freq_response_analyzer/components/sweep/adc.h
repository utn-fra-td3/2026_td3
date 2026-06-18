#ifndef ADC_H
#define ADC_H

// --- Includes ---
#include <stdint.h>

// --- Prototipos publicos ---
void adc_init(void);
int  adc_read_vin_mv(void);
int  adc_read_vout_mv(void);

#endif // ADC_H
