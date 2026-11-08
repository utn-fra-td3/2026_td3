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
    return ad8302_read_raw(channel, out_value);
}

esp_err_t sweep_run(const sweep_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->start_hz == 0U || cfg->start_hz > cfg->stop_hz ||
        cfg->stop_hz > ad9834_get_max_frequency_hz() || cfg->step_hz == 0U ||
        cfg->step_hz > ad9834_get_max_frequency_hz() ||
        cfg->dwell_ms == 0U || cfg->dwell_ms > 60000U ||
        (cfg->measurement_source != AD8302_SOURCE_ADC && cfg->measurement_source != AD8302_SOURCE_SIM)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Inicio de barrido: %lu Hz -> %lu Hz, paso %lu Hz", 
             (unsigned long)cfg->start_hz,
             (unsigned long)cfg->stop_hz,
             (unsigned long)cfg->step_hz);

    uint32_t sequence = 0;
    for (uint32_t freq = cfg->start_hz; ; freq += cfg->step_hz) {
        if (cfg->measurement_callback == NULL || cfg->measurement_source == AD8302_SOURCE_ADC) {
            ad9834_set_frequency_hz(freq, AD9834_FREQ_REG0);
        }
        TickType_t settle_ticks = pdMS_TO_TICKS(cfg->dwell_ms - 1U) + 1U;
        vTaskDelay(settle_ticks);

        esp_err_t ret;
        if (cfg->measurement_callback != NULL) {
            ad8302_sample_t sample;
            ret = ad8302_measure(freq, cfg->measurement_source, &sample);
            if (ret == ESP_OK) {
                cfg->measurement_callback(&sample, sequence, cfg->sample_context);
            }
        } else {
            uint32_t adc_value;
            ret = read_adc_value(cfg->adc_channel, &adc_value);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "f = %lu Hz | ADC = %lu", (unsigned long)freq, (unsigned long)adc_value);
                if (cfg->sample_callback != NULL) {
                    cfg->sample_callback(freq, adc_value, cfg->sample_context);
                }
            }
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Measurement failed at %lu Hz: %s", (unsigned long)freq, esp_err_to_name(ret));
            return ret;
        }
        sequence++;
        if (cfg->stop_hz - freq < cfg->step_hz) {
            break;
        }
    }

    return ESP_OK;
}
