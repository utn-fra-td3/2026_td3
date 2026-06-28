#include "system_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "adc_sensor.h" 
#include "display_tft.h" 
#include "../eeprom/include/eeprom.h" 

static const char *TAG = "SYS_MANAGER";

// --- DEFINICIÓN DE PINES PARA LEDs ---
#define LED_MIN_PIN     15
#define LED_MAX_PIN     16

extern QueueHandle_t encoder_queue;
extern SemaphoreHandle_t button_sem;
extern QueueHandle_t pid_cfg_queue;
extern QueueHandle_t display_queue; 

void task_system_manager(void *pvParameters) {
    eeprom_init();

    // Configurar los pines de los LEDs como salidas
    gpio_reset_pin(LED_MIN_PIN);
    gpio_set_direction(LED_MIN_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(LED_MAX_PIN);
    gpio_set_direction(LED_MAX_PIN, GPIO_MODE_OUTPUT);

    system_mode_t current_mode = MODE_CC;
    float setpoint_cc = 0.0f; 
    float setpoint_cr = 10.0f; 
    int encoder_delta = 0;

    if (eeprom_load_config(&current_mode, &setpoint_cc, &setpoint_cr)) {
        ESP_LOGW(TAG, "Estado de la EEPROM recuperado con éxito");
    } else {
        ESP_LOGE(TAG, "EEPROM vacía. Usando valores por defecto.");
        eeprom_save_config(current_mode, setpoint_cc, setpoint_cr);
    }

    pid_config_t initial_cfg = {
        .mode = current_mode, 
        .setpoint = (current_mode == MODE_CC) ? setpoint_cc : setpoint_cr
    };
    xQueueOverwrite(pid_cfg_queue, &initial_cfg);

    if (display_queue != NULL) {
        ui_update_t init_disp_msg = { .source = UI_MSG_FROM_SYSMAN, .mode = current_mode, .setpoint = initial_cfg.setpoint };
        xQueueSend(display_queue, &init_disp_msg, 0);
    }

    TickType_t last_change_time = 0;
    bool eeprom_sync_needed = false;

    while (1) {
        bool update_needed = false;

        // Botón
        if (xSemaphoreTake(button_sem, 0) == pdTRUE) {
            current_mode = (current_mode == MODE_CC) ? MODE_CR : MODE_CC;
            ESP_LOGW(TAG, "Cambio de Modo: %s", current_mode == MODE_CC ? "CC" : "CR");
            update_needed = true;
        }

        // Encoder
        if (xQueueReceive(encoder_queue, &encoder_delta, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (current_mode == MODE_CC) {
                setpoint_cc += (encoder_delta * 0.1f);
                if (setpoint_cc < 0.0f) setpoint_cc = 0.0f;
                if (setpoint_cc > 3.0f) setpoint_cc = 3.0f; 
            } else {
                setpoint_cr += (encoder_delta * 1.0f);
                if (setpoint_cr < 1.0f) setpoint_cr = 1.0f;
                if (setpoint_cr > 100.0f) setpoint_cr = 100.0f;
            }
            update_needed = true;
        }

        if (update_needed) {
            pid_config_t next_cfg;
            next_cfg.mode = current_mode;
            next_cfg.setpoint = (current_mode == MODE_CC) ? setpoint_cc : setpoint_cr;
            xQueueOverwrite(pid_cfg_queue, &next_cfg); 

            if (display_queue != NULL) {
                ui_update_t disp_msg = { .source = UI_MSG_FROM_SYSMAN, .mode = current_mode, .setpoint = next_cfg.setpoint };
                xQueueSend(display_queue, &disp_msg, 0);
            }
            
            // LOGICA DE CONTROL DE LOS LEDs INDICADORES
            if (current_mode == MODE_CC) {
                gpio_set_level(LED_MIN_PIN, (setpoint_cc <= 0.0f) ? 1 : 0);
                gpio_set_level(LED_MAX_PIN, (setpoint_cc >= 3.0f) ? 1 : 0);
            } else {
                gpio_set_level(LED_MIN_PIN, (setpoint_cr <= 1.0f) ? 1 : 0);
                gpio_set_level(LED_MAX_PIN, (setpoint_cr >= 100.0f) ? 1 : 0);
            }

            ESP_LOGI(TAG, "Enviada nueva config al PID y Display.");
            eeprom_sync_needed = true;
            last_change_time = xTaskGetTickCount();
        }

        if (eeprom_sync_needed && (xTaskGetTickCount() - last_change_time) > pdMS_TO_TICKS(2000)) {
            eeprom_save_config(current_mode, setpoint_cc, setpoint_cr);
            eeprom_sync_needed = false; 
        }
    }
}