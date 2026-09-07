#ifndef ADC_SENSOR_H
#define ADC_SENSOR_H

#include <stdint.h>

// Esta estructura empaqueta nuestras mediciones para no enviar variables sueltas
typedef struct {
    float voltage_v; // Voltaje de la fuente a probar (ej. 12.5 V)
    float current_a; // Corriente medida en el shunt (ej. 1.25 A)
} sensor_data_t;

// Exponemos únicamente la tarea para que main.c la pueda lanzar
void task_adc_read(void *pvParameters);

#endif // ADC_SENSOR_H