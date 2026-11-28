#ifndef AD9833_H
#define AD9833_H

#include <stdint.h>

void ad9833_init(void);
void ad9833_set_frequency(uint32_t freq_hz);

#endif // AD9833_H