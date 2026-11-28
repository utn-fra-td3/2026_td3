#include "adc_driver.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#define TAG "ADC_DRIVER"

// ADC1_CH6 = GPIO34
// ADC1_CH7 = GPIO35
#define ADC_UNIT     ADC_UNIT_1
#define ADC_CH6      ADC_CHANNEL_6
#define ADC_CH7      ADC_CHANNEL_7
#define ADC_ATTEN    ADC_ATTEN_DB_12   // rango 0–3.3V

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t         cali_handle = NULL;
static bool                      cali_ok     = false;

void adc_driver_init(void) {
    // Inicializar unidad ADC1
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    // Configurar canal 6
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CH6, &chan_cfg));

    // Configurar canal 7
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CH7, &chan_cfg));

    // Calibración (usa curva si está disponible, si no usa línea)
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
        cali_ok = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT,
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle) == ESP_OK) {
        cali_ok = true;
    }
#endif

    if (!cali_ok) {
        ESP_LOGW(TAG, "Calibracion no disponible, se usa conversion lineal");
    }
}

// Devuelve tension en mV (0–3300)
uint32_t adc_read_mv(uint8_t canal) {
    adc_channel_t ch = (canal == 6) ? ADC_CH6 : ADC_CH7;

    // Promedio de 8 muestras para reducir ruido
    int32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        adc_oneshot_read(adc_handle, ch, &raw);
        sum += raw;
    }
    int raw_avg = (int)(sum / 8);

    int mv = 0;
    if (cali_ok) {
        adc_cali_raw_to_voltage(cali_handle, raw_avg, &mv);
    } else {
        // Conversión lineal sin calibración: 12 bits → 0–3300 mV
        mv = (int)((raw_avg * 3300) / 4095);
    }
    return (uint32_t)mv;
}