#ifndef MEDYPARAM_H
#define MEDYPARAM_H

#include <stdint.h>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define NUM_MUESTRAS   100

#define CENTRO_V       1.57f    // Valor de referencia (V) para la señal de entrada, que corresponde a 0V en la señal diferencial.

typedef struct
{
    float muestras[NUM_MUESTRAS];   
    float nivel_trigger;            
    config_t config_usada;         
} medyparam_t;

extern QueueHandle_t enviar;               

/* Nivel de trigger en mV, en las mismas unidades que las muestras
 * (centrado en CENTRO_V y multiplicado por el factor de la escala). */
int medyparam_trigger_mv_escalado(uint8_t escala);

void medyparam_init(void);
void task_medyparam(void *param);

#endif
