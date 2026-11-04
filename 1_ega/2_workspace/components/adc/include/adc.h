#ifndef ADC_H
#define ADC_H

#include <stdint.h>
#include "esp_err.h"

#define ADC_CANAL_SENAL       0
#define ADC_CANAL_TRIGGER     1

#define ADC_RAW_MAX           4095
#define ADC_VREF_MV           3300

#define ADC_SAMPLE_FREQ_HZ    83333   // Freq. de muestreo total (2 canales) = 83.333 kHz, freq. de muestreo por canal = 41.666 kHz

typedef struct
{
    uint8_t canal;
    uint16_t raw;
} adc_muestra_t;

void adc_init(void);

esp_err_t adc_start(void);
esp_err_t adc_stop(void);

// Lee un frame del ADC continuo y separa las muestras por canal.
// Devuelve ESP_OK, ESP_ERR_TIMEOUT (sin datos) u otro error del driver.
esp_err_t adc_leer_muestras(
    adc_muestra_t *muestras,        // Puntero al arreglo donde se guardarán las muestras leídas.
    uint16_t max_muestras,          // Cantidad máxima de muestras que se pueden almacenar en el arreglo.
    uint16_t *cantidad_muestras     // Puntero a la variable donde se almacenará la cantidad de muestras leídas.
);

uint16_t adc_raw_a_mv(uint16_t raw);

#endif
