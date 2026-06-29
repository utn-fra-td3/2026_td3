#include "system_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "adc_sensor.h" 
#include "display_tft.h" 
#include "../eeprom/include/eeprom.h" 
#include <math.h> // <-- NUEVO: Necesario para roundf() y evitar errores de coma flotante

static const char *TAG = "SYS_MANAGER";

// --- DEFINICIÓN DE PINES PARA LEDs ---
#define LED_MIN_PIN     15
#define LED_MAX_PIN     16

// --- DEFINICIÓN DE EVENTOS DE BOTONES ---
#define EVENT_SW_PRESSED     1
#define EVENT_CONFIG_PRESSED 2

extern QueueHandle_t encoder_queue;
extern QueueHandle_t button_queue; // <-- CAMBIO: Ahora es Cola, no Semáforo
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

    // --- VARIABLES DE LA MÁQUINA DE ESTADOS ---
    uint8_t ui_state = 0;       // 0: Pantalla Monitor, 1: Pantalla Configuración
    uint8_t cursor_pos = 1;     // 0: Cambiar Modo, 1..3: Editar dígitos numéricos
    uint8_t button_event = 0;

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
        // ACTUALIZADO: Pasamos también el ui_state y el cursor_pos inicial
        ui_update_t init_disp_msg = { 
            .source = UI_MSG_FROM_SYSMAN, 
            .mode = current_mode, 
            .setpoint = initial_cfg.setpoint,
            .ui_state = ui_state,
            .cursor_pos = cursor_pos
        };
        xQueueSend(display_queue, &init_disp_msg, 0);
    }

    TickType_t last_change_time = 0;
    bool eeprom_sync_needed = false;

    while (1) {
        bool update_needed = false;

        // ==========================================
        // EVENTO 1: LECTURA DE BOTONES
        // ==========================================
        if (xQueueReceive(button_queue, &button_event, 0) == pdTRUE) {
            
            if (button_event == EVENT_CONFIG_PRESSED) {
                // Entrar / Salir de Configuración
                ui_state = (ui_state == 0) ? 1 : 0;
                cursor_pos = 1; // Reiniciamos el cursor al primer dígito por comodidad
                
                // Si salimos de configuración, forzamos un guardado instantáneo en la memoria
                if (ui_state == 0) {
                    eeprom_save_config(current_mode, setpoint_cc, setpoint_cr);
                    eeprom_sync_needed = false;
                    ESP_LOGI(TAG, "Saliendo de CONFIG. Valores guardados.");
                }
            } 
            else if (button_event == EVENT_SW_PRESSED && ui_state == 1) {
                // El SW solo hace algo si estamos en Modo Configuración
                cursor_pos++;
                uint8_t max_cursor = (current_mode == MODE_CC) ? 3 : 2; 
                if (cursor_pos > max_cursor) cursor_pos = 0; // Si pasa del max, salta a 0 (Editar Modo)
            }
            update_needed = true;
        }

        // ==========================================
        // EVENTO 2: LECTURA DEL ENCODER
        // ==========================================
        if (xQueueReceive(encoder_queue, &encoder_delta, pdMS_TO_TICKS(20)) == pdTRUE) {
            
            // SOLO permitimos cambiar valores si la pantalla está en CONFIG
            if (ui_state == 1) { 
                
                if (cursor_pos == 0) {
                    // Si el cursor está en 0, girar cambia entre CC y CR
                    if (encoder_delta != 0) {
                        current_mode = (current_mode == MODE_CC) ? MODE_CR : MODE_CC;
                        cursor_pos = 1; // Reseteamos cursor al cambiar de modo
                    }
                } 
                else {
                    // Modificamos el valor matemáticamente según el dígito activo
                    float step = 0;
                    if (current_mode == MODE_CC) {
                        if (cursor_pos == 1) step = 1.0f;       // Unidades enteras
                        else if (cursor_pos == 2) step = 0.1f;  // Décimas
                        else if (cursor_pos == 3) step = 0.01f; // Centésimas
                        
                        setpoint_cc += (encoder_delta * step);
                        setpoint_cc = roundf(setpoint_cc * 100.0f) / 100.0f; // Evita basurilla decimal
                        
                        if (setpoint_cc < 0.0f) setpoint_cc = 0.0f;
                        if (setpoint_cc > 3.0f) setpoint_cc = 3.0f;
                    } 
                    else { // MODE CR
                        if (cursor_pos == 1) step = 10.0f;      // Decenas
                        else if (cursor_pos == 2) step = 1.0f;  // Unidades
                        
                        setpoint_cr += (encoder_delta * step);
                        setpoint_cr = roundf(setpoint_cr);
                        
                        if (setpoint_cr < 1.0f) setpoint_cr = 1.0f;
                        if (setpoint_cr > 100.0f) setpoint_cr = 100.0f;
                    }
                }
                update_needed = true;
            }
        }

        // ==========================================
        // ACTUALIZACIÓN GENERAL (Si hubo cambios)
        // ==========================================
        if (update_needed) {
            pid_config_t next_cfg;
            next_cfg.mode = current_mode;
            next_cfg.setpoint = (current_mode == MODE_CC) ? setpoint_cc : setpoint_cr;
            
            // Actualizamos el PID inmediatamente
            xQueueOverwrite(pid_cfg_queue, &next_cfg); 

            // Actualizamos la Pantalla informándole el nuevo estado (Monitor o Config)
            if (display_queue != NULL) {
                ui_update_t disp_msg = { 
                    .source = UI_MSG_FROM_SYSMAN, 
                    .mode = current_mode, 
                    .setpoint = next_cfg.setpoint,
                    .ui_state = ui_state,         // <-- Vital para que la pantalla cambie de color
                    .cursor_pos = cursor_pos      // <-- Vital para que resalte el dígito correcto
                };
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

            ESP_LOGI(TAG, "Estado UI: %d | Cursor: %d | Modo: %d | SP: %.2f", ui_state, cursor_pos, current_mode, next_cfg.setpoint);
            
            // Si hicimos cambios estando en CONFIG, reseteamos el temporizador anti-desgaste
            if (ui_state == 1) {
                eeprom_sync_needed = true;
                last_change_time = xTaskGetTickCount();
            }
        }

        // GUARDADO DE RESPALDO (Timeout 2 seg)
        // Por si el usuario se queda en la pantalla de CONFIG sin salir
        if (eeprom_sync_needed && (xTaskGetTickCount() - last_change_time) > pdMS_TO_TICKS(2000)) {
            eeprom_save_config(current_mode, setpoint_cc, setpoint_cr);
            eeprom_sync_needed = false; 
        }
    }
}