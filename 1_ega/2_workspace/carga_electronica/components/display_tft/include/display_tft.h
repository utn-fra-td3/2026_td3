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
    float voltage;
    float current;
    system_mode_t mode;
    float setpoint;
    
    // --- VARIABLES VITALES PARA EL MENÚ ---
    uint8_t ui_state;   // 0 o 1
    uint8_t cursor_pos; // 0 a 3
} ui_update_t;

void task_display_update(void *pvParameters);

#endif