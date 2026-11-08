#ifndef AD8302_MEASUREMENT_H
#define AD8302_MEASUREMENT_H

#include <stdint.h>
#include "esp_err.h"

#define AD8302_MAG_CHANNEL 0
#define AD8302_PHASE_CHANNEL 6
#define AD8302_SAMPLE_COUNT 32U
#define AD8302_FLAG_SIMULATED 1U
#define AD8302_FLAG_ADC_UNCALIBRATED 2U
#define AD8302_FLAG_RANGE_SUSPECT 4U
#define AD8302_FLAG_DETECTOR_UNCALIBRATED 8U

typedef enum {
    AD8302_SOURCE_ADC = 0,
    AD8302_SOURCE_SIM = 1,
} ad8302_source_t;

typedef struct {
    uint32_t frequency_hz;
    int64_t timestamp_us;
    int mag_raw;
    int phase_raw;
    int mag_mv;
    int phase_mv;
    int mag_span_raw;
    int phase_span_raw;
    uint32_t flags;
} ad8302_sample_t;

esp_err_t ad8302_measurement_init(void);
esp_err_t ad8302_read_raw(int channel, uint32_t *value);
esp_err_t ad8302_measure(uint32_t frequency_hz, ad8302_source_t source,
                         ad8302_sample_t *sample);

#endif