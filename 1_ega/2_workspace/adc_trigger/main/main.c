#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_continuous.h"

// ====================================================================
// CONFIGURACIONES GENERALES DEL PERIFÉRICO
// ====================================================================
#define EXAMPLE_ADC_UNIT             ADC_UNIT_1
#define EXAMPLE_ADC_CONV_MODE        ADC_CONV_SINGLE_UNIT_1
#define EXAMPLE_ADC_ATTEN            ADC_ATTEN_DB_12
#define EXAMPLE_ADC_BIT_WIDTH        SOC_ADC_DIGI_MAX_BITWIDTH
#define EXAMPLE_READ_LEN            256 

// Canal Seguro para ESP32-S3: ADC1_CH2 corresponde físicamente al GPIO 3
static adc_channel_t channel[1] = {ADC_CHANNEL_6}; 

// Handles de las Tareas de FreeRTOS
static TaskHandle_t xAdcTaskHandle = NULL;
static TaskHandle_t xTriggerTaskHandle = NULL;

// Semáforos Binarios de Sincronización (Según tu diagrama de bloques)
static SemaphoreHandle_t set_trigger_event = NULL;
static SemaphoreHandle_t set_ADC_event = NULL;

// Recursos y Memoria Compartida
#define COLA_MUESTRAS_MAX 64
static uint16_t buffer_compartido_muestras[COLA_MUESTRAS_MAX];
static uint32_t ultima_muestra_analizada = 0;

// Prototipo de la función de inicialización del hardware
static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle);

// ====================================================================
// ISR_DMA: Interrupción por hardware al completarse un frame (256 bytes)
// ====================================================================
static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    // Envía una notificación directa de alta velocidad a la Tarea_ADC
    vTaskNotifyGiveFromISR(xAdcTaskHandle, &mustYield);
    return (mustYield == pdTRUE);
}

// ====================================================================
// TAREA TRIGGER (Prioridad 4 - Procesamiento Matemático Concurrente)
// ====================================================================
void vTriggerTask(void *pvParameters) {
    while(1) {
        // Se bloquea de forma eficiente esperando que Tarea_ADC cargue muestras nuevas
        if (xSemaphoreTake(set_trigger_event, portMAX_DELAY) == pdTRUE) {
            
            for (int i = 0; i < COLA_MUESTRAS_MAX; i++) {
                uint32_t muestra_actual = buffer_compartido_muestras[i];

                // Algoritmo de Trigger: Busca cruce por umbral medio (2048) en flanco de subida
                if (muestra_actual > 2048 && ultima_muestra_analizada <= 2048) {
                    // Da luz verde a la Tarea_ADC para que libere la ráfaga
                    xSemaphoreGive(set_ADC_event);
                    break; // Corta el bucle para evitar falsos disparos múltiples en el mismo lote
                }
                ultima_muestra_analizada = muestra_actual;
            }
        }
    }
}

// ====================================================================
// TAREA ADC (Prioridad 4 - Adquisición por DMA y Transmisión Serie)
// ====================================================================
void vAdcTask(void *pvParameters) {
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[EXAMPLE_READ_LEN] = {0};

    adc_continuous_handle_t handle = NULL;
    continuous_adc_init(channel, 1, &handle); 

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = s_conv_done_cb };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    while (1) {
        // Bloqueo total (0% CPU) hasta que la ISR de hardware de aviso
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (1) {
            ret = adc_continuous_read(handle, result, EXAMPLE_READ_LEN, &ret_num, 0);
            
            if (ret == ESP_OK) {
                uint32_t samples_count = ret_num / SOC_ADC_DIGI_RESULT_BYTES;
                adc_continuous_data_t parsed_data[samples_count];
                uint32_t num_parsed_samples = 0;

                esp_err_t parse_ret = adc_continuous_parse_data(handle, result, ret_num, parsed_data, &num_parsed_samples);
                
                if (parse_ret == ESP_OK) {
                    // 1. Resguardo de muestras analógicas válidas en memoria compartida
                    for (int i = 0; i < num_parsed_samples && i < COLA_MUESTRAS_MAX; i++) {
                        if (parsed_data[i].valid) {
                            buffer_compartido_muestras[i] = parsed_data[i].raw_data;
                        }
                    }

                    // 2. Despierta a la Tarea_Trigger de inmediato entregando el semáforo
                    xSemaphoreGive(set_trigger_event);

                    // 3. SINCRONIZACIÓN DETERMINISTA: Cede la CPU y espera hasta 10ms la decisión del Trigger
                    if (xSemaphoreTake(set_ADC_event, pdMS_TO_TICKS(10)) == pdTRUE) {
                        
                        // ¡DISPARO CONFIRMADO! Envía la ráfaga a SerialPlot de inmediato
                        for (int i = 0; i < num_parsed_samples && i < COLA_MUESTRAS_MAX; i++) {
                            if (parsed_data[i].valid) {
                                // Agregamos el cast (uint32_t) para que coincida con la macro PRIu32
                                printf("%"PRIu32"\n", (uint32_t)buffer_compartido_muestras[i]);
                            }
                        }
                    }
                    // Si pasaron los 10ms y el trigger no dio el OK, el lote se ignora (Efecto congelado)
                }
            } else if (ret == ESP_ERR_TIMEOUT) {
                // Buffer de hardware vacío, rompe el bucle interno y vuelve a esperar la ISR
                break;
            }
        }
    }
}

// ====================================================================
// TAREA MAIN (Configuradora e Inicializadora del Sistema)
// ====================================================================
void app_main(void)
{
    // Creación segura de los semáforos binarios
    set_trigger_event = xSemaphoreCreateBinary();
    set_ADC_event = xSemaphoreCreateBinary();

    // Lanzamiento de las tareas concurrentes compartiendo la misma prioridad alta (4)
    xTaskCreate(vAdcTask, "Tarea_ADC", 4096, NULL, 4, &xAdcTaskHandle);
    xTaskCreate(vTriggerTask, "Tarea_Trigger", 4096, NULL, 4, &xTriggerTaskHandle);
}

// ====================================================================
// INICIALIZACIÓN INTERNA DEL DRIVER DE ESP-IDF
// ====================================================================
static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle) {
    adc_continuous_handle_t handle = NULL;
    
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = EXAMPLE_READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 1 * 1000, // Fijado a 1 kHz para control analógico estable
        .conv_mode = EXAMPLE_ADC_CONV_MODE,
    };
    
    adc_digi_pattern_config_t adc_pattern[1] = {0};
    dig_cfg.pattern_num = channel_num;
    
    adc_pattern[0].atten = EXAMPLE_ADC_ATTEN;
    adc_pattern[0].channel = channel[0] & 0x7;
    adc_pattern[0].unit = EXAMPLE_ADC_UNIT;
    adc_pattern[0].bit_width = EXAMPLE_ADC_BIT_WIDTH;
    
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
    *out_handle = handle;
}