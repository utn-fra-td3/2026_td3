#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#include "../../system_manager/include/system_manager.h"
#include "../../adc_sensor/include/adc_sensor.h"

// Identificador de quién envía el mensaje
typedef enum {
    UI_MSG_FROM_ADC,
    UI_MSG_FROM_SYSMAN
} ui_msg_source_t;

// Estructura del paquete
typedef struct {
    ui_msg_source_t source;
    
    // Datos si el source es ADC
    float voltage;
    float current;
    
    // Datos si el source es SYSMAN
    system_mode_t mode;
    float setpoint;
} ui_update_t;

void task_display_update(void *pvParameters);

#endif