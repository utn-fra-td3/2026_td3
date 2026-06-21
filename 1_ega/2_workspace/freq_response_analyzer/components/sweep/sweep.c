// --- Includes ---
#include "sweep.h"
#include "ad9833.h"
#include "adc.h"
#include "esp_log.h"
#include "driver/gpio.h"

// --- Defines privados ---
#define SWEEP_PIN_CTRL_1   GPIO_NUM_6
#define SWEEP_PIN_CTRL_2   GPIO_NUM_16

// --- Variables privadas ---
static const char *TAG = "sweep";

// --- Funciones ---

void task_sweep(void *pvParameters)
{
    int mv_vin = 0;
    int mv_vout = 0;

    gpio_set_direction(SWEEP_PIN_CTRL_1, GPIO_MODE_OUTPUT);
    gpio_set_direction(SWEEP_PIN_CTRL_2, GPIO_MODE_OUTPUT);
    gpio_set_level(SWEEP_PIN_CTRL_1, 0);
    gpio_set_level(SWEEP_PIN_CTRL_2, 0);

    ad9833_init();
    adc_init();

    /*Prueba de DDS*/
    ad9833_set_freq(10000);
    ESP_LOGI(TAG, "AD9833 set freq 10 kHz");
    while (1)
    {
        ad9833_enable_output();
        ESP_LOGI(TAG, "AD9833 ON");
        vTaskDelay(pdMS_TO_TICKS(5000));

        //ad9833_disable_output();
        //ESP_LOGI(TAG, "AD9833 OFF");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    /*Prueba de ADC*/
    /*
    while (1)
    {
        mv_vin = adc_read_vin_mv();
        mv_vout = adc_read_vout_mv();

        ESP_LOGI(TAG, "VIN mV=%4d  VOUT mV=%4d", mv_vin, mv_vout);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
    */
}
