#include "as5600_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"

#include "app_config.h"

esp_err_t as5600_read_raw(uint16_t *raw)
{
    // El AS5600 entrega el angulo en 12 bits. Se leen los dos registros de
    // angulo y se enmascara el resultado para descartar bits no usados.
    uint8_t reg = AS5600_REG_ANGLE_H;
    uint8_t data[2] = {0};

    esp_err_t err = i2c_master_write_read_device(
        I2C_PORT,
        AS5600_ADDR,
        &reg,
        1,
        data,
        2,
        pdMS_TO_TICKS(50)
    );

    if (err != ESP_OK) {
        return err;
    }

    *raw = ((uint16_t)data[0] << 8) | data[1];
    *raw &= 0x0FFF;

    return ESP_OK;
}
