#ifndef FREQUENCY_SWEEP_H
#define FREQUENCY_SWEEP_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t start_hz;
    uint32_t stop_hz;
    uint32_t step_hz;
    uint32_t dwell_ms;
    int adc_channel;
} sweep_config_t;

esp_err_t sweep_run(const sweep_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
