#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define med_stateoff_config 0
#define med_staterun_config 1

/* Escalas reales usadas (calibradas): 200 mV/div y 500 mV/div */
#define escala_200mv_div 0
#define escala_500mv_div 1

/* Bases de tiempo. La pantalla del visualizador dibuja 12 divisiones horizontales
 * y el ADC muestrea a 24 us/punto (41,666 kHz/canal). El tiempo por division es:
 *   ms/div = NUM_MUESTRAS * avg * 24us / 12 divisiones
 * Con NUM_MUESTRAS=100 y avg=1/2/5 (ver avg_por_base) quedan 0,2 / 0,4 / 1,0 ms/div,
 * acordes a las señales de 1-2 kHz del ensayo. */
#define base_0p2ms_div 0
#define base_0p4ms_div 1
#define base_1p0ms_div 2

#define flanco_ascendente 0
#define flanco_descendente 1

/* Comandos por la cola intro:
 *   1..7  -> teclado / boton fisico (toggles, ver task_config)
 *   10+N  -> fijar escala vertical = N  (N = 0..1)   [desde la Raspberry]
 *   20+N  -> fijar base de tiempo  = N  (N = 0..2)   [desde la Raspberry]
 *   30+N  -> fijar flanco          = N  (N = 0..1)   [desde la Raspberry]
 * Los comandos absolutos evitan depender del estado actual (un toggle
 * repetido o perdido dejaria la configuracion en otro valor). */
#define CMD_SET_ESCALA   10
#define CMD_SET_BASE     20
#define CMD_SET_FLANCO   30

typedef uint8_t intro_t; // tipo de dato para comandos que se mandan por la cola intro

typedef uint16_t trigger_mv_t;

typedef struct 
{
    uint8_t estado;
    uint8_t escala_vertical;
    uint8_t base_tiempo;
    uint8_t flanco;
    uint16_t nivel_trigger;
} config_t; 
 
extern QueueHandle_t intro; 
extern QueueHandle_t config;
extern QueueHandle_t trigger_mv;

void config_init(void);
void task_config(void *param);

#endif
