#ifndef _HELPER_H_
#define _HELPER_H_

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

// Prototipo
void pwm_user_init(uint32_t gpio, uint32_t freq);

#endif