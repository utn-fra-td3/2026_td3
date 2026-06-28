#ifndef ADC_SENSOR_H
#define ADC_SENSOR_H

#include <stdint.h>

// struct de mediciones
typedef struct {
    float voltage_v; 
    float current_a; 
} sensor_data_t;

void task_adc_read(void *pvParameters);

#endif