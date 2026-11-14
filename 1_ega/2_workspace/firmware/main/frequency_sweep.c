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

static bool sweep_cancelled(const sweep_config_t *cfg)
{
    return cfg->cancel_callback != NULL && cfg->cancel_callback(cfg->sample_context);
}

static esp_err_t wait_for_sample(const sweep_config_t *cfg, TickType_t ticks)
{
    TickType_t started = xTaskGetTickCount();
    TickType_t interval = pdMS_TO_TICKS(20);
    if (interval == 0) interval = 1;
    while (true) {
        if (sweep_cancelled(cfg)) return SWEEP_ERR_CANCELLED;
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= ticks) break;
        TickType_t remaining = ticks - elapsed;
        vTaskDelay(cfg->cancel_callback != NULL && remaining > interval ? interval : remaining);
    }
    return sweep_cancelled(cfg) ? SWEEP_ERR_CANCELLED : ESP_OK;
}

esp_err_t sweep_validate(const sweep_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->start_hz < AD9834_MIN_FREQUENCY_HZ || cfg->start_hz > cfg->stop_hz ||
        cfg->stop_hz > ad9834_get_max_frequency_hz() ||
        (cfg->measurement_source != AD8302_SOURCE_ADC && cfg->measurement_source != AD8302_SOURCE_SIM)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->point_count != 0U) {
        if (cfg->point_count > 10000U || cfg->point_count > cfg->stop_hz - cfg->start_hz + 1U ||
            (cfg->point_count == 1U && cfg->start_hz != cfg->stop_hz) ||
            cfg->total_time_ms == 0U || cfg->total_time_ms > 3600000U ||
            (uint64_t)cfg->total_time_ms * configTICK_RATE_HZ < (uint64_t)cfg->point_count * 1000U) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (cfg->step_hz == 0U || cfg->step_hz > ad9834_get_max_frequency_hz() ||
               cfg->dwell_ms == 0U || cfg->dwell_ms > 60000U || cfg->total_time_ms != 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t sweep_run(const sweep_config_t *cfg)
{
    esp_err_t validation = sweep_validate(cfg);
    if (validation != ESP_OK) {
        return validation;
    }
    bool counted = cfg->point_count != 0U;
    uint32_t points = counted ? cfg->point_count : (cfg->stop_hz - cfg->start_hz) / cfg->step_hz + 1U;
    TickType_t total_ticks = (TickType_t)(((uint64_t)cfg->total_time_ms * configTICK_RATE_HZ + 999U) / 1000U);
    TickType_t started = xTaskGetTickCount();

    ESP_LOGI(TAG, "Inicio de barrido: %lu Hz -> %lu Hz, paso %lu Hz", 
             (unsigned long)cfg->start_hz,
             (unsigned long)cfg->stop_hz,
             (unsigned long)cfg->step_hz);

    for (uint32_t sequence = 0; sequence < points; sequence++) {
        if (sweep_cancelled(cfg)) return SWEEP_ERR_CANCELLED;
        uint32_t freq = cfg->start_hz;
        if (counted && points > 1U) {
            freq += (uint32_t)(((uint64_t)(cfg->stop_hz - cfg->start_hz) * sequence) / (points - 1U));
        } else if (!counted) {
            freq += sequence * cfg->step_hz;
        }
        if (cfg->measurement_callback == NULL || cfg->measurement_source == AD8302_SOURCE_ADC) {
            ad9834_set_frequency_hz(freq, AD9834_FREQ_REG0);
        }
        TickType_t settle_ticks = counted ? 0U : pdMS_TO_TICKS(cfg->dwell_ms - 1U) + 1U;
        if (counted) {
            TickType_t deadline = (TickType_t)(((uint64_t)(sequence + 1U) * total_ticks) / points);
            TickType_t elapsed = xTaskGetTickCount() - started;
            if (elapsed >= deadline) {
                ESP_LOGW(TAG, "Sweep timing budget exceeded at point %lu", (unsigned long)sequence);
                return ESP_ERR_TIMEOUT;
            }
            settle_ticks = deadline - elapsed;
        }
        esp_err_t wait_result = wait_for_sample(cfg, settle_ticks);
        if (wait_result != ESP_OK) return wait_result;

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
        if (sweep_cancelled(cfg)) return SWEEP_ERR_CANCELLED;
    }
    if (counted && xTaskGetTickCount() - started > total_ticks + 1U) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
