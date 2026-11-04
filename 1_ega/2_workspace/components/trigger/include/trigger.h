#ifndef TRIGGER_H
#define TRIGGER_H

#include <stdint.h>

#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct
{
    uint8_t disparar_medicion;
    config_t config_trigger;
} trigger_t;

extern QueueHandle_t trigger;

void trigger_init(void);

void task_configtrigger(void *param);
void task_disparotrigger(void *param);

#endif