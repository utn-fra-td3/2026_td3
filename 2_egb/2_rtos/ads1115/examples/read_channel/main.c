#include <stdio.h>

#include "ads1115.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define I2C_PORT i2c0
#define I2C_SDA_GPIO 12U
#define I2C_SCL_GPIO 13U

int main(void) {
    stdio_init_all();
    i2c_init(I2C_PORT, 100U * 1000U);
    gpio_set_function(I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_GPIO);
    gpio_pull_up(I2C_SCL_GPIO);

    ads1115_t ads;
    if (!ads1115_init(&ads, I2C_PORT, ADS1115_ADDRESS_GND, ADS1115_GAIN_4_096V, ADS1115_DATA_RATE_860_SPS)) panic("Configuracion ADS1115 invalida");

    while (true) {
        uint16_t raw = 0U;
        if (ads1115_read_single_ended_blocking(&ads, 0U, &raw)) {
            float pin_voltage = ads1115_raw_to_voltage(&ads, (int16_t)raw);
            float input_voltage = ads1115_restore_divider_voltage(pin_voltage, 330000.0f, 22000.0f);
            printf("RAW=%u Pin=%.4f V Entrada=%.3f V\n", raw, pin_voltage, input_voltage);
        } else {
            printf("Error de lectura ADS1115\n");
        }
        sleep_ms(250U);
    }
}
