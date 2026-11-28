#ifndef TIPOS_SISTEMA_H
#define TIPOS_SISTEMA_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Parámetros de barrido (Enviados desde Menú -> Adquisición)
typedef struct {
    int freq_inicial;
    int freq_final;
    int paso;
    int tiempo_ms;
} param_barrido_t;

// Datos crudos de tensión (Enviados desde Adquisición -> Procesamiento)
typedef struct {
    int frecuencia_hz;
    float vin_crudo;
    float vout_crudo;
} datos_crudos_t;

// Colas utilizadas en el sitema
typedef struct {
    QueueHandle_t q_encoder;          // Eventos físicos del encoder
    QueueHandle_t q_comandos_barrido; // Dispara el ciclo de barrido
    QueueHandle_t q_datos_crudos;     // Envía lecturas ADC para cálculos matemáticos
} colas_sistema_t;

#endif // TIPOS_SISTEMA_H