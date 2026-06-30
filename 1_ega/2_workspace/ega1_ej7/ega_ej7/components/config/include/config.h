#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define med_stateoff_config 0
#define med_staterun_config 1

#define escala_1v_div 0
#define escala_5v_div 1

#define base_1ms_div 0
#define base_2ms_div 1
#define base_5ms_div 2

#define flanco_ascendente 0
#define flanco_descendente 1

typedef uint8_t intro_t; // tipo de dato para comandos que se mandan por la cola intro

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

void config_init(void);
void task_config(void *param);

#endif