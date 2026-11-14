#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_system.h"

#include "ad9834_driver.h"
#include "ad8302_measurement.h"
#include "frequency_sweep.h"
#include "uart_console.h"
#include "local_hmi.h"

static const char *TAG = "EGA_FREQ_ANALYZER";

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando analizador de respuesta en frecuencia");

    ad9834_cfg_t ad9834_cfg = {
        .mosi_gpio = GPIO_NUM_23,
        .sclk_gpio = GPIO_NUM_18,
        .cs_gpio = GPIO_NUM_5,
        .reset_gpio = GPIO_NUM_4,
        .freq_register = AD9834_FREQ_REG0,
        .mclk_hz = 75000000UL,
    };

    esp_err_t ret = ad9834_init(&ad9834_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar AD9834: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ad9834_set_output_sine();
    ad9834_set_frequency_hz(AD9834_MIN_FREQUENCY_HZ, AD9834_FREQ_REG0);

    ret = ad8302_measurement_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC measurement unavailable: %s", esp_err_to_name(ret));
    }

    ret = uart_console_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART tasks unavailable: %s", esp_err_to_name(ret));
        ad9834_enable(false);
        return;
    }

    ESP_LOGI(TAG, "Generacion configurada; consultar STATUS para el estado restaurado");
    ESP_LOGI(TAG, "Terminal UART lista. Enviar HELP para ver comandos.");

#ifdef CONFIG_EGA_HMI
    ret = local_hmi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Local HMI unavailable: %s", esp_err_to_name(ret));
    }
#endif
}
