#include "driver/i2c.h"
#include "esp_log.h"
#include <stdio.h>

esp_err_t eeprom_write_byte(uint16_t mem_addr, uint8_t data);
esp_err_t eeprom_read_byte(uint16_t mem_addr, uint8_t *data);