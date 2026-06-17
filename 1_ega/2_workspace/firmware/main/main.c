#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "i2c_bus.h"
#include "ina219.h"
#include "mcp4725.h"
#include "LCD.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_ERROR_CHECK(i2c_bus_init());

    ina219_t ina;
    ESP_ERROR_CHECK(ina219_init(&ina, INA219_ADDR_GND_GND));
    ESP_ERROR_CHECK(ina219_configure(&ina,
        INA219_BUS_RANGE_32V,
        INA219_GAIN_0_125,
        INA219_RES_12BIT_1S,
        INA219_RES_12BIT_1S,
        INA219_MODE_CONT_SHUNT_BUS));
    ESP_ERROR_CHECK(ina219_calibrate(&ina, 0.05));
    ESP_LOGI(TAG, "INA219 ready");

    mcp4725_t dac;
    ESP_ERROR_CHECK(mcp4725_init(&dac, MCP4725_ADDR_A0_GND));
    ESP_ERROR_CHECK(mcp4725_set_voltage(&dac, 3.3, 0.5));
    ESP_LOGI(TAG, "MCP4725 set to 0.5V");

    lcd_handle_t lcd;
    ESP_ERROR_CHECK(lcd_init(LCD_I2C_ADDR_DEFAULT, &lcd));
    lcd_clear(&lcd);
    ESP_LOGI(TAG, "LCD ready");

    vTaskDelay(pdMS_TO_TICKS(100));

    float v_shunt, current;
    while (1) {
        ina219_get_shunt_voltage(&ina, &v_shunt);
        ina219_get_current(&ina, &current);

        ESP_LOGI(TAG, "Vshunt=%.6fV  I=%.4fA", v_shunt, current);

        lcd_set_cursor(&lcd, 0, 0);
        lcd_printf(&lcd, "Vs=%.4fV ", v_shunt);
        lcd_set_cursor(&lcd, 0, 1);
        lcd_printf(&lcd, "I =%.3fA ", current);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
