#include "driver/i2c.h"
#include <stdio.h>
#include "esp_log.h"

esp_err_t oled_send_command(uint8_t command);
esp_err_t oled_send_data(uint8_t data);
void oled_init_minimal(void);
void oled_set_cursor(uint8_t col, uint8_t page);
void oled_clear(void);
void oled_print_digit(uint8_t digit);
void oled_print_number(uint16_t number);

