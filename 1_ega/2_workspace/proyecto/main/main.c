#include <stdio.h>
#include <inttypes.h> // Para PRId32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"
#include "driver/ledc.h"
#include "esp_log.h"

#define ENCODER_A_GPIO          4   // PIN CLK del KY-040
#define ENCODER_B_GPIO          5   // PIN DT del KY-040
#define LED_TEST_GPIO           6   // PIN del LED (salida PWM)

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_9_BIT // Resolución de 9 bits
#define LEDC_FREQUENCY          80000             // 80KHz

typedef enum {
    TIPO_INCREMENTO,
    TIPO_ABSOLUTO
} tipo_dato_t;

typedef struct {
    tipo_dato_t tipo;
    int32_t valor;
} mensaje_entrada_t;

// Handles de las colas de comunicación asíncrona
QueueHandle_t xColaEntrada = NULL;
QueueHandle_t xColaPWM = NULL;

// --- PROTOTIPOS DE CONFIGURACIÓN ---
pcnt_unit_handle_t init_encoder_pcnt(void);
void init_ledc_pwm(void);

// --- 1. TAREA SINTONIA (Core 0 - Prioridad 3) ---
void tarea_sintonia(void *pvParameters) {
    pcnt_unit_handle_t encoder = (pcnt_unit_handle_t)pvParameters;
    int pulse_count = 0;
    int last_pulse_count = 0;

    while (1) {
        ESP_ERROR_CHECK(pcnt_unit_get_count(encoder, &pulse_count));
        
        int delta = pulse_count - last_pulse_count;
        if (delta != 0) {
            mensaje_entrada_t msg = {
                .tipo = TIPO_INCREMENTO,
                .valor = delta
            };
            xQueueSend(xColaEntrada, &msg, portMAX_DELAY);
            last_pulse_count = pulse_count;
        }
        vTaskDelay(pdMS_TO_TICKS(40)); 
    }
}

// --- 2. TAREA DATO (Core 0 - Prioridad 4) ---
void tarea_dato(void *pvParameters) {
    mensaje_entrada_t msg_recibido;
    int32_t brillo_actual = 2048; // 50% de 12 bits

    while (1) {
        if (xQueueReceive(xColaEntrada, &msg_recibido, portMAX_DELAY) == pdTRUE) {
            if (msg_recibido.tipo == TIPO_INCREMENTO) {
                brillo_actual += (msg_recibido.valor * 50);
            } else if (msg_recibido.tipo == TIPO_ABSOLUTO) {
                brillo_actual = msg_recibido.valor;
            }

            // Límites estrictos 12 bits (0 a 4095)
            if (brillo_actual > 4095) brillo_actual = 4095;
            if (brillo_actual < 0) brillo_actual = 0;

            xQueueSend(xColaPWM, &brillo_actual, portMAX_DELAY);
        }
    }
}

// --- 3. TAREA PWM_ANALOG (Core 0 - Prioridad 3) ---
void tarea_pwm_analog(void *pvParameters) {
    int32_t nuevo_duty = 0;

    while (1) {
        if (xQueueReceive(xColaPWM, &nuevo_duty, portMAX_DELAY) == pdTRUE) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, nuevo_duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
            // Uso de PRId32 para evitar alertas de formato del compilador
            ESP_LOGI("PWM_TEST", "Ciclo de Trabajo LED actualizado a: %"PRId32"/4095", nuevo_duty);
        }
    }
}

// --- PUNTO DE ENTRADA (Ejecutándose ya bajo el scheduler) ---
void app_main(void) {
    // 1. Inicializar Hardware
    pcnt_unit_handle_t encoder_handle = init_encoder_pcnt();
    init_ledc_pwm();

    // 2. Crear Colas de Comunicación Segura
    xColaEntrada = xQueueCreate(10, sizeof(mensaje_entrada_t));
    xColaPWM     = xQueueCreate(5, sizeof(int32_t));

    if (xColaEntrada != NULL && xColaPWM != NULL) {
        // 3. Desplegar Topología de Tareas en Core 0
        xTaskCreatePinnedToCore(tarea_sintonia,   "TASK_SINTONIA",   3072, (void*)encoder_handle, 3, NULL, 0);
        xTaskCreatePinnedToCore(tarea_dato,       "TASK_DATO",       3072, NULL,                  4, NULL, 0);
        xTaskCreatePinnedToCore(tarea_pwm_analog, "TASK_PWM_ANALOG", 2048, NULL,                  3, NULL, 0);
        
        ESP_LOGI("MAIN", "Topología del Encoder y LED iniciada con éxito en Core 0.");
    } else {
        ESP_LOGE("MAIN", "Error crítico al inicializar las colas de FreeRTOS.");
    }
    
    // Al terminar app_main, la tarea "main_task" se destruye automáticamente en segundo plano.
}

// --- IMPLEMENTACIÓN DE DRIVERS ESP-IDF ---
pcnt_unit_handle_t init_encoder_pcnt(void) {
    pcnt_unit_config_t unit_config = {
        .high_limit = 100,
        .low_limit = -100,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

   pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, // Filtro físico 1µs
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));
 
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_A_GPIO,
        .level_gpio_num = ENCODER_B_GPIO,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    
    return pcnt_unit;
}

void init_ledc_pwm(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_TEST_GPIO,
        .duty           = 0, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}
