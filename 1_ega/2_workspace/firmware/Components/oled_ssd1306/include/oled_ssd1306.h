#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define I2C_OLED_SDA_PIN    16
#define I2C_OLED_SCL_PIN    15
#define I2C_OLED_PORT       I2C_NUM_0
#define SSD1306_I2C_ADDR    0x3C

esp_err_t oled_init(void);
void oled_clear(void);
void oled_update(void);
void oled_draw_string(uint8_t x, uint8_t page, const char *str, bool invert);

#endif // OLED_SSD1306_H