#ifndef AD9833_H
#define AD9833_H

// --- Includes ---
#include <stdint.h>

// --- Prototipos publicos ---
void ad9833_init(void);
void ad9833_set_freq(uint32_t freq_hz);
void ad9833_enable_output(void);
void ad9833_disable_output(void);

#endif // AD9833_H
