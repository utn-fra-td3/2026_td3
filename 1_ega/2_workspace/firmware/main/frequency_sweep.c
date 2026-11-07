#include "frequency_sweep.h"

#include <stdio.h>

#include "ad9834_driver.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FREQ_SWEEP";

static esp_err_t read_adc_value(int channel, uint32_t *out_value)
{
    if (out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ret = adc_oneshot_config_channel(adc_handle, (adc_channel_t)channel, &chan_cfg);
    if (ret == ESP_OK) {
        int raw = 0;
        ret = adc_oneshot_read(adc_handle, (adc_channel_t)channel, &raw);
        if (ret == ESP_OK) {
            *out_value = (uint32_t)raw;
        }
    }

    adc_oneshot_del_unit(adc_handle);
    return ret;
}

esp_err_t sweep_run(const sweep_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->start_hz == 0U || cfg->stop_hz == 0U || cfg->step_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Inicio de barrido: %lu Hz -> %lu Hz, paso %lu Hz", 
             (unsigned long)cfg->start_hz,
             (unsigned long)cfg->stop_hz,
             (unsigned long)cfg->step_hz);

    for (uint32_t freq = cfg->start_hz; freq <= cfg->stop_hz; freq += cfg->step_hz) {
        ad9834_set_frequency_hz(freq, AD9834_FREQ_REG0);

        uint32_t adc_value = 0;
        esp_err_t ret = read_adc_value(cfg->adc_channel, &adc_value);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "No se pudo leer el ADC en %lu Hz: %s", (unsigned long)freq, esp_err_to_name(ret));
        }

        ESP_LOGI(TAG, "f = %lu Hz | ADC = %lu", (unsigned long)freq, (unsigned long)adc_value);
        vTaskDelay(pdMS_TO_TICKS(cfg->dwell_ms));
    }

    return ESP_OK;
}
