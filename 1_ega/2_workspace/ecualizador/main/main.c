#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_dsp.h"
#include "i2s_init.h"


void app_main(void)
{
    ESP_LOGI("APP", "Inicializando I2S en modo full-duplex...");
    ESP_ERROR_CHECK(i2s_init_full_duplex());
    ESP_LOGI("APP", "I2S inicializado correctamente.");


    while (1) {
    }
}