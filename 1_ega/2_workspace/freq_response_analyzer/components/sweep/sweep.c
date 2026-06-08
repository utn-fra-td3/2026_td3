// --- Includes ---
#include "sweep.h"
#include "ad9833.h"
#include "adc.h"
#include "esp_log.h"

// --- Variables privadas ---
static const char *TAG = "sweep";

// --- Funciones ---

void task_sweep(void *pvParameters)
{
    int mv_vin = 0;
    int mv_vout = 0;

    //ad9833_init();
    adc_init();

    while (1)
    {
        mv_vin = adc_read_vin_mv();
        mv_vout = adc_read_vout_mv();

        ESP_LOGI(TAG, "VIN mV=%4d  VOUT mV=%4d", mv_vin, mv_vout);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
