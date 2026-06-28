#include "encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ENCODER";

#define ENCODER_PIN_A       4   
#define ENCODER_PIN_B       5
#define ENCODER_PIN_SW      6   

extern QueueHandle_t encoder_queue;
extern SemaphoreHandle_t button_sem;

static pcnt_unit_handle_t pcnt_unit = NULL;

// --- INTERRUPCIÓN DEL BOTÓN CON ANTIRREBOTE (ISR) ---
static void IRAM_ATTR button_isr_handler(void* arg) {
    // Variable estática, recuerda el último valor
    static TickType_t last_isr_time = 0;
    
    // Obtengo el tiempo actual
    TickType_t current_time = xTaskGetTickCountFromISR();

    // Comparo. 250ms
    if ((current_time - last_isr_time) > pdMS_TO_TICKS(250)) {
        
        // doy el semaforo para despertar al System Manager
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(button_sem, &xHigherPriorityTaskWoken);
        
        // Fuerzo cambio de prioridad si el System Manager tiene mayor prioridad
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR(); 
        }
        
        // Guardo actual para bloquear los siguientes rebotes
        last_isr_time = current_time;
    }
}

static void encoder_init_hardware(void) {
    // Configurar PCNT
    pcnt_unit_config_t unit_config = {
        .high_limit = 100,
        .low_limit = -100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_PIN_A, // GPIO4
        .level_gpio_num = ENCODER_PIN_B, // GPIO5
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // Reglas de conteo para cuadratura (Giro horario/antihorario)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    
    // filtro de Glitch para rebotes mecánicos
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 5000, // Filtra ruidos menores a 5 microsegundos
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // configuro Boton
    gpio_config_t btn_config = {
        .pin_bit_mask = (1ULL << ENCODER_PIN_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE // Interrupción al presionar
    };
    gpio_config(&btn_config);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_PIN_SW, button_isr_handler, NULL);
}

void task_encoder_read(void *pvParameters) {
    encoder_init_hardware();
    int pulse_count = 0;

    while (1) {
        pcnt_unit_get_count(pcnt_unit, &pulse_count);
        
        if (pulse_count != 0) {
            // Enviamos el Delta a la cola del System Manager
            if (xQueueSend(encoder_queue, &pulse_count, 0) == pdTRUE) {
                // Si se envió bien, reseteamos el hardware a 0
                pcnt_unit_clear_count(pcnt_unit);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}