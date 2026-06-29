#include "encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ENCODER";

// --- PINES ---
#define ENCODER_PIN_A       4   
#define ENCODER_PIN_B       5
#define ENCODER_PIN_SW      6   
#define CONFIG_BUTTON_PIN   3   // <-- NUEVO BOTON DE CONFIGURACIÓN

// --- EVENTOS ---
#define EVENT_SW_PRESSED     1  // Cambiar dígito
#define EVENT_CONFIG_PRESSED 2  // Entrar/Salir Config

extern QueueHandle_t encoder_queue;
extern QueueHandle_t button_queue; // <-- CAMBIO: Ahora es una Cola, no un semáforo

static pcnt_unit_handle_t pcnt_unit = NULL;

// --- INTERRUPCIÓN UNIFICADA PARA AMBOS BOTONES (ISR) ---
static void IRAM_ATTR buttons_isr_handler(void* arg) {
    // Obtenemos qué pin disparó la interrupción
    int pin = (int)arg;
    
    // Variables estáticas independientes para el antirrebote de cada botón
    static TickType_t last_isr_time_sw = 0;
    static TickType_t last_isr_time_cfg = 0;
    
    TickType_t current_time = xTaskGetTickCountFromISR();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t event = 0;

    // Evaluamos el botón del Encoder (SW)
    if (pin == ENCODER_PIN_SW) {
        if ((current_time - last_isr_time_sw) > pdMS_TO_TICKS(250)) {
            event = EVENT_SW_PRESSED;
            xQueueSendFromISR(button_queue, &event, &xHigherPriorityTaskWoken);
            last_isr_time_sw = current_time;
        }
    } 
    // Evaluamos el nuevo botón de Configuración
    else if (pin == CONFIG_BUTTON_PIN) {
        if ((current_time - last_isr_time_cfg) > pdMS_TO_TICKS(250)) {
            event = EVENT_CONFIG_PRESSED;
            xQueueSendFromISR(button_queue, &event, &xHigherPriorityTaskWoken);
            last_isr_time_cfg = current_time;
        }
    }

    // Forzamos cambio de contexto si se despertó una tarea de mayor prioridad
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR(); 
    }
}

static void encoder_init_hardware(void) {
    // --- 1. Configurar PCNT (Queda exactamente igual) ---
    pcnt_unit_config_t unit_config = {
        .high_limit = 100,
        .low_limit = -100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_PIN_A, 
        .level_gpio_num = ENCODER_PIN_B, 
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 5000, 
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // --- 2. Configurar AMBOS Botones ---
    gpio_config_t btn_config = {
        // Enmascaramos los dos pines al mismo tiempo usando el operador OR (|)
        .pin_bit_mask = (1ULL << ENCODER_PIN_SW) | (1ULL << CONFIG_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Interrupción al presionar
    };
    gpio_config(&btn_config);
    
    // Instalamos el servicio global de interrupciones
    gpio_install_isr_service(0);
    
    // Enganchamos el MISMO handler a ambos pines, pero le pasamos el número de pin como argumento
    gpio_isr_handler_add(ENCODER_PIN_SW, buttons_isr_handler, (void*)ENCODER_PIN_SW);
    gpio_isr_handler_add(CONFIG_BUTTON_PIN, buttons_isr_handler, (void*)CONFIG_BUTTON_PIN);
}

void task_encoder_read(void *pvParameters) {
    encoder_init_hardware();
    int pulse_count = 0;

    while (1) {
        pcnt_unit_get_count(pcnt_unit, &pulse_count);
        
        if (pulse_count != 0) {
            if (xQueueSend(encoder_queue, &pulse_count, 0) == pdTRUE) {
                pcnt_unit_clear_count(pcnt_unit);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}