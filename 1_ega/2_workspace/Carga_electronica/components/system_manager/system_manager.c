#include "system_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
// Incluimos para poder leer el struct del ADC si lo necesitamos
#include "adc_sensor.h" 
#include "../display_tft/include/display_tft.h" // Para poder enviar los datos al display
#include "driver/gpio.h"
#include "pin_setting.h"

static const char *TAG = "SYS_MANAGER";

extern QueueHandle_t adc_queue;
extern QueueHandle_t pid_cfg_queue; // Conectamos con main.c
extern QueueHandle_t display_queue;
extern QueueHandle_t sysman_queue;

void task_system_manager(void *pvParameters) {
    system_mode_t current_mode = MODE_CC;
    float setpoint_cc = 0.0f; 
    float setpoint_cr = 10.0f; 
    float setpoint_cv = 0.0f;
    sysman_msg_t msg;

    // Inicializar pines de alarmas LED
    gpio_set_direction(ALARM_OVP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ALARM_OCP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ALARM_OVP_PIN, 0);
    gpio_set_level(ALARM_OCP_PIN, 0);

    // Enviamos una configuración inicial al arrancar para que el PID no empiece en el aire
    pid_config_t initial_cfg = {
        .mode = MODE_CC,
        .setpoint = 0.0f,
        .force_stop = false,
    };
    xQueueOverwrite(pid_cfg_queue, &initial_cfg);

    float limit_ovp = 15.0f; // Default OVP 15V
    float limit_ocp = 0.5f;  // Default OCP 500mA
    bool alarm_active = false;
    bool alarms_armed = false;

    // --- NUEVO: Enviamos el estado inicial a la pantalla al arrancar ---
    if (display_queue != NULL) {
        ui_update_t init_disp_msg;
        init_disp_msg.source = UI_MSG_FROM_SYSMAN;
        init_disp_msg.mode = current_mode;
        init_disp_msg.setpoint = setpoint_cc;
        xQueueSend(display_queue, &init_disp_msg, 0);
    }

    while (1) {
        bool update_needed = false;

        // EVENTO: Mensajes desde la Interfaz Gráfica (LVGL) o ADC
        if (xQueueReceive(sysman_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (msg.type == SYSMAN_MSG_MODE_CHANGE) {
                current_mode = msg.data.new_mode;
                ESP_LOGW(TAG, "Cambio de Modo desde UI: %d", current_mode);
                update_needed = true;
                // Al cambiar modo manual, podemos intentar resetear la alarma si el usuario está interviniendo,
                // o requerir un reset explícito. Por ahora lo dejamos simple.
            } else if (msg.type == SYSMAN_MSG_SETPOINT_CHANGE) {
                if (current_mode == MODE_CC) {
                    setpoint_cc = msg.data.new_setpoint;
                } else if (current_mode == MODE_CR) {
                    setpoint_cr = msg.data.new_setpoint;
                } else if (current_mode == MODE_CV) {
                    setpoint_cv = msg.data.new_setpoint;
                }
                update_needed = true;
            } else if (msg.type == SYSMAN_MSG_ALARM_LIMIT_CHANGE) {
                if (msg.data.alarm_limit.type == ALARM_OVP) {
                    limit_ovp = msg.data.alarm_limit.new_limit;
                    ESP_LOGI(TAG, "Nuevo límite OVP: %.2f V", limit_ovp);
                } else if (msg.data.alarm_limit.type == ALARM_OCP) {
                    limit_ocp = msg.data.alarm_limit.new_limit;
                    ESP_LOGI(TAG, "Nuevo límite OCP: %.3f A", limit_ocp);
                }
            } else if (msg.type == SYSMAN_MSG_ARM_ALARMS) {
                alarm_active = false;
                alarms_armed = true;
                gpio_set_level(ALARM_OVP_PIN, 0);
                gpio_set_level(ALARM_OCP_PIN, 0);
                update_needed = true;
                ESP_LOGI(TAG, "Alarmas armadas para RUNNING: OVP %.2f V, OCP %.3f A",
                         limit_ovp, limit_ocp);
            } else if (msg.type == SYSMAN_MSG_ADC_UPDATE) {
                // Chequeo de límites (ALARMAS)
                if (!alarm_active && !alarms_armed) {
                    continue;
                }
                bool trigger_ovp = (msg.data.adc.voltage > limit_ovp);
                bool trigger_ocp = (msg.data.adc.current > limit_ocp);

                if (trigger_ocp) {
                    ESP_LOGW(TAG, "OCP: %.3f A supera limite %.3f A",
                             msg.data.adc.current, limit_ocp);
                }
                
                if ((trigger_ovp || trigger_ocp) && !alarm_active) {
                    alarm_active = true;
                    update_needed = true; // Forzar update al PID
                    ESP_LOGE(TAG, "ALARM TRIGGERED! OVP: %d, OCP: %d", trigger_ovp, trigger_ocp);
                    
                    // Encender LEDs físicos
                    if (trigger_ovp) gpio_set_level(ALARM_OVP_PIN, 1);
                    if (trigger_ocp) gpio_set_level(ALARM_OCP_PIN, 1);

                    // Avisar al display
                    if (display_queue != NULL) {
                        ui_update_t alarm_msg;
                        alarm_msg.source = UI_MSG_ALARM_TRIGGERED;
                        // Usamos mode para pasar qué alarma es (truco temporal)
                        alarm_msg.mode = trigger_ovp ? ALARM_OVP : ALARM_OCP; 
                        xQueueSend(display_queue, &alarm_msg, 0);
                    }
                }
            }
        }

        // Si algo cambió, le informamos al PID y a la Pantalla
        if (update_needed) {
            // (Envío al PID que ya tenías)
            pid_config_t next_cfg;
            next_cfg.mode = current_mode;
            next_cfg.force_stop = alarm_active;
            
            if (current_mode == MODE_CC) next_cfg.setpoint = setpoint_cc;
            else if (current_mode == MODE_CR) next_cfg.setpoint = setpoint_cr;
            else next_cfg.setpoint = setpoint_cv;
            
            xQueueOverwrite(pid_cfg_queue, &next_cfg); 

            // --- NUEVO: ENVÍO A LA PANTALLA ---
            if (display_queue != NULL) {
                ui_update_t disp_msg;
                disp_msg.source = UI_MSG_FROM_SYSMAN; // ¡Soy el System Manager!
                disp_msg.mode = current_mode;
                disp_msg.setpoint = next_cfg.setpoint;
                // No nos importan voltage ni current aquí

                xQueueSend(display_queue, &disp_msg, 0);
            }
            
            ESP_LOGI(TAG, "Enviada nueva config al PID y Display.");
        }
    }
}