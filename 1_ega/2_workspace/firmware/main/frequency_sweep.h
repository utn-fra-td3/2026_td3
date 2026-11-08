#ifndef FREQUENCY_SWEEP_H
#define FREQUENCY_SWEEP_H

#include <stdint.h>

#include "esp_err.h"
#include "ad8302_measurement.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*sweep_sample_callback_t)(uint32_t frequency_hz, uint32_t adc_value, void *context);
typedef void (*sweep_measurement_callback_t)(const ad8302_sample_t *sample, uint32_t sequence, void *context);

typedef struct {
    uint32_t start_hz;
    uint32_t stop_hz;
    uint32_t step_hz;
    uint32_t dwell_ms;
    int adc_channel;
    sweep_sample_callback_t sample_callback;
    void *sample_context;
    ad8302_source_t measurement_source;
    sweep_measurement_callback_t measurement_callback;
} sweep_config_t;

esp_err_t sweep_run(const sweep_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
