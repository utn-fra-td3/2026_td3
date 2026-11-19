// --- Includes ---
#include "adc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

// --- Defines privados ---
#define ADC_ATTEN_SWEEP     ADC_ATTEN_DB_12     // atenuación de 12dB
#define ADC_BITWIDTH_SWEEP  ADC_BITWIDTH_12     // resolución de 12 bits

#define ADC_CHANNEL_VIN     ADC_CHANNEL_1       // GPIO2
#define ADC_CHANNEL_VOUT    ADC_CHANNEL_3       // GPIO4

// --- Variables privadas ---
static const char *TAG = "adc";

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc_cali_vin_handle = NULL;
static adc_cali_handle_t adc_cali_vout_handle = NULL;

// --- Prototipos privados ---
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static int  adc_read_mv(adc_channel_t channel, adc_cali_handle_t cali_handle);

// --- Funciones ---

void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_SWEEP,
        .bitwidth = ADC_BITWIDTH_SWEEP,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_VIN, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_VOUT, &chan_cfg));

    adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_VIN, ADC_ATTEN_SWEEP, &adc_cali_vin_handle);
    adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_VOUT, ADC_ATTEN_SWEEP, &adc_cali_vout_handle);

    ESP_LOGI(TAG, "ADC inicializado");
}

int adc_read_vin_mv(void)
{
    return adc_read_mv(ADC_CHANNEL_VIN, adc_cali_vin_handle);
}

int adc_read_vout_mv(void)
{
    return adc_read_mv(ADC_CHANNEL_VOUT, adc_cali_vout_handle);
}

static int adc_read_mv(adc_channel_t channel, adc_cali_handle_t cali_handle)
{
    int raw = 0;
    int mv = 0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, channel, &raw));
    adc_cali_raw_to_voltage(cali_handle, raw, &mv);

    return mv;
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_SWEEP,
    };
    bool calibrado = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &handle) == ESP_OK);

    *out_handle = handle;

    if (calibrado) {
        ESP_LOGI(TAG, "Calibracion ADC canal %d OK", channel);
    } else {
        ESP_LOGW(TAG, "Calibracion ADC canal %d no disponible (eFuse no grabado)", channel);
    }

    return calibrado;
}
