#ifndef AD9834_DRIVER_H
#define AD9834_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AD9834_MCLK_DEFAULT 75000000UL

typedef enum {
    AD9834_FREQ_REG0 = 0,
    AD9834_FREQ_REG1 = 1,
} ad9834_frequency_register_t;

typedef struct {
    gpio_num_t mosi_gpio;
    gpio_num_t sclk_gpio;
    gpio_num_t cs_gpio;
    gpio_num_t reset_gpio;
    ad9834_frequency_register_t freq_register;
    uint32_t mclk_hz;
} ad9834_cfg_t;

esp_err_t ad9834_init(const ad9834_cfg_t *cfg);
uint32_t ad9834_get_max_frequency_hz(void);
void ad9834_reset(void);
void ad9834_enable(bool enabled);
void ad9834_set_output_sine(void);
void ad9834_set_output_triangle(void);
void ad9834_set_output_square(void);
void ad9834_set_frequency_hz(uint32_t frequency_hz, ad9834_frequency_register_t register_select);
void ad9834_write_register(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
