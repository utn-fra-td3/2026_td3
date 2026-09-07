#ifndef DISPLAY_TFT_H
#define DISPLAY_TFT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "system_manager.h" // Necesario para system_mode_t

typedef enum {
    UI_MSG_FROM_ADC,
    UI_MSG_FROM_SYSMAN,
    UI_MSG_ALARM_TRIGGERED
} ui_msg_source_t;

typedef struct {
    ui_msg_source_t source;
    float voltage;
    float current;
    system_mode_t mode;
    float setpoint;
} ui_update_t;

void task_display_update(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_TFT_H
