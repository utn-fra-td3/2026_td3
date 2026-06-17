#ifndef I2C_BUS_H
#define I2C_BUS_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

esp_err_t i2c_bus_init(void);

esp_err_t i2c_bus_write(uint8_t dev_addr, const uint8_t *data, size_t len);

esp_err_t i2c_bus_read(uint8_t dev_addr, uint8_t *data, size_t len);

esp_err_t i2c_bus_write_read(uint8_t dev_addr,
                              const uint8_t *write_data, size_t write_len,
                              uint8_t *read_data, size_t read_len);

#endif
