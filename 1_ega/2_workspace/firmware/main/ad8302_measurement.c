#include "ad8302_measurement.h"

#include <math.h>
#include <stddef.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "AD8302_MEAS";
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_calibration;

esp_err_t ad8302_measurement_init(void)
{
    if (s_adc != NULL) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (ret != ESP_OK) {
        return ret;
    }
    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(s_adc, AD8302_MAG_CHANNEL, &channel_cfg);
    if (ret == ESP_OK) {
        ret = adc_oneshot_config_channel(s_adc, AD8302_PHASE_CHANNEL, &channel_cfg);
    }
    if (ret != ESP_OK) {
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return ret;
    }

    adc_cali_line_fitting_efuse_val_t calibration_type;
    ret = adc_cali_scheme_line_fitting_check_efuse(&calibration_type);
    if (ret == ESP_OK && calibration_type != ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
        adc_cali_line_fitting_config_t calibration_cfg = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
            .default_vref = 0,
        };
        ret = adc_cali_create_scheme_line_fitting(&calibration_cfg, &s_calibration);
        if (ret != ESP_OK) {
            s_calibration = NULL;
        }
    }
    ESP_LOGI(TAG, "ADC1: MAG GPIO36, PHASE GPIO34; eFuse calibration %s",
             s_calibration != NULL ? "available" : "unavailable (raw only)");
    return ESP_OK;
}

esp_err_t ad8302_read_raw(int channel, uint32_t *value)
{
    if (value == NULL || (channel != AD8302_MAG_CHANNEL && channel != AD8302_PHASE_CHANNEL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int raw;
    esp_err_t ret = adc_oneshot_read(s_adc, channel, &raw);
    if (ret == ESP_OK) {
        *value = (uint32_t)raw;
    }
    return ret;
}

static int simulation_voltage(double voltage_mv, uint32_t *flags)
{
    if (voltage_mv < 30.0 || voltage_mv > 1770.0) {
        *flags |= AD8302_FLAG_RANGE_SUSPECT;
    }
    return (int)lround(fmax(30.0, fmin(1800.0, voltage_mv)));
}

esp_err_t ad8302_measure(uint32_t frequency_hz, ad8302_source_t source,
                         ad8302_sample_t *sample)
{
    if (sample == NULL || frequency_hz == 0 ||
        (source != AD8302_SOURCE_ADC && source != AD8302_SOURCE_SIM)) {
        return ESP_ERR_INVALID_ARG;
    }
    ad8302_sample_t result = {
        .frequency_hz = frequency_hz,
        .timestamp_us = esp_timer_get_time(),
        .mag_raw = -1,
        .phase_raw = -1,
        .mag_mv = -1,
        .phase_mv = -1,
        .mag_span_raw = -1,
        .phase_span_raw = -1,
        .flags = AD8302_FLAG_DETECTOR_UNCALIBRATED,
    };
    if (source == AD8302_SOURCE_SIM) {
        double ratio = (double)frequency_hz / 100000.0;
        double gain_db = -10.0 * log10(1.0 + ratio * ratio);
        double phase_deg = atan(ratio) * 180.0 / 3.141592653589793;
        result.flags |= AD8302_FLAG_SIMULATED;
        result.mag_mv = simulation_voltage(900.0 + 30.0 * gain_db, &result.flags);
        result.phase_mv = simulation_voltage(1800.0 - 10.0 * phase_deg, &result.flags);
        *sample = result;
        return ESP_OK;
    }
    if (s_adc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t mag_sum = 0;
    uint32_t phase_sum = 0;
    int mag_min = 4095;
    int mag_max = 0;
    int phase_min = 4095;
    int phase_max = 0;
    for (uint32_t index = 0; index < AD8302_SAMPLE_COUNT; index++) {
        uint32_t mag_raw;
        uint32_t phase_raw;
        esp_err_t ret = ad8302_read_raw(AD8302_MAG_CHANNEL, &mag_raw);
        if (ret == ESP_OK) {
            ret = ad8302_read_raw(AD8302_PHASE_CHANNEL, &phase_raw);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        mag_sum += mag_raw;
        phase_sum += phase_raw;
        if ((int)mag_raw < mag_min) mag_min = (int)mag_raw;
        if ((int)mag_raw > mag_max) mag_max = (int)mag_raw;
        if ((int)phase_raw < phase_min) phase_min = (int)phase_raw;
        if ((int)phase_raw > phase_max) phase_max = (int)phase_raw;
    }
    result.mag_raw = (int)((mag_sum + AD8302_SAMPLE_COUNT / 2) / AD8302_SAMPLE_COUNT);
    result.phase_raw = (int)((phase_sum + AD8302_SAMPLE_COUNT / 2) / AD8302_SAMPLE_COUNT);
    result.mag_span_raw = mag_max - mag_min;
    result.phase_span_raw = phase_max - phase_min;
    if (mag_min <= 16 || mag_max >= 4079 || phase_min <= 16 || phase_max >= 4079) {
        result.flags |= AD8302_FLAG_RANGE_SUSPECT;
    }
    if (s_calibration == NULL) {
        result.flags |= AD8302_FLAG_ADC_UNCALIBRATED;
    } else {
        esp_err_t ret = adc_cali_raw_to_voltage(s_calibration, result.mag_raw, &result.mag_mv);
        if (ret == ESP_OK) {
            ret = adc_cali_raw_to_voltage(s_calibration, result.phase_raw, &result.phase_mv);
        }
        if (ret != ESP_OK) {
            return ret;
        }
        if (result.mag_mv <= 30 || result.mag_mv >= 1770 ||
            result.phase_mv <= 30 || result.phase_mv >= 1770) {
            result.flags |= AD8302_FLAG_RANGE_SUSPECT;
        }
    }
    *sample = result;
    return ESP_OK;
}