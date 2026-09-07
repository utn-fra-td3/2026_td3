#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_system.h"

// Componentes
#include "mcp4725.h"
#include "encoder.h"
#include "eeprom.h"
#include "adc_sensor.h"
#include "system_manager.h"
#include "pid_control.h"
#include "display_tft.h"
#include "pin_setting.h"
#include "communication_setting.h"

// Declaración global de las vías de comunicación
QueueHandle_t adc_queue = NULL; // Cola de sensores
QueueHandle_t encoder_queue = NULL; // Cola del encoder
SemaphoreHandle_t button_sem = NULL; // Habilita o no el DAC
QueueHandle_t pid_cfg_queue = NULL; // Envía el modo/setpoint del SysManager al PID
QueueHandle_t dac_queue = NULL;    // Envía el valor de 12 bits (0-4095) del PID al DAC
SemaphoreHandle_t i2c_mutex = NULL; // Candado para que la EEPROM y el DAC no choquen
QueueHandle_t display_queue = NULL; // Envia órdenes desde LVGL (pantalla)
QueueHandle_t sysman_queue = NULL; // Recibe órdenes desde LVGL (pantalla)


void app_main(void) {
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI("MAIN", "Causa del ultimo reset: %d", reset_reason);
    ESP_LOGI("MAIN", "Heap libre al arrancar: %lu bytes", (unsigned long)esp_get_free_heap_size());

   // Actualizamos el Log para reflejar la integración de los gráficos
    ESP_LOGI("MAIN", "Iniciando Fase 4: Visualización Gráfica (TFT + LVGL)...");

    // 1. Inicializar Hardware Global
    if (communication_i2c_init() == ESP_OK) {
        ESP_LOGI("MAIN", "Bus I2C maestro inicializado con éxito.");
    } else {
        ESP_LOGE("MAIN", "Fallo crítico al inicializar I2C. Sistema detenido.");
        return;
    }

    if (communication_spi_init() == ESP_OK) {
        ESP_LOGI("MAIN", "Bus SPI inicializado con éxito.");
    } else {
        ESP_LOGE("MAIN", "Fallo crítico al inicializar SPI. Sistema detenido.");
        return;
    }

    // 2. Creación de recursos de FreeRTOS
    adc_queue       = xQueueCreate(1, sizeof(sensor_data_t)); 
    encoder_queue   = xQueueCreate(10, sizeof(int));
    button_sem      = xSemaphoreCreateBinary();
    pid_cfg_queue   = xQueueCreate(1, sizeof(pid_config_t));  
    dac_queue       = xQueueCreate(2, sizeof(uint16_t));
    i2c_mutex       = xSemaphoreCreateMutex(); 

    // Capacidad para acumular hasta 5 actualizaciones de telemetría/estado
    display_queue   = xQueueCreate(30, sizeof(ui_update_t));
    sysman_queue    = xQueueCreate(10, sizeof(sysman_msg_t));

    // Agregamos 'display_queue' y 'sysman_queue' a la validación de seguridad
    if (adc_queue && encoder_queue && button_sem && pid_cfg_queue && dac_queue && i2c_mutex && display_queue && sysman_queue) {
        
        // 3. Lanzamiento de Tareas (Con sus prioridades estratégicas)

        xTaskCreate(task_adc_read,       "Task_ADC",     4096, NULL, 5, NULL); 
        xTaskCreate(task_pid_compute,    "Task_PID",     4096, NULL, 5, NULL); 
        xTaskCreate(task_dac_update,     "Task_DAC",     3072, NULL, 5, NULL); 
        xTaskCreate(task_system_manager, "Task_SysMan",  4096, NULL, 4, NULL);
        xTaskCreate(task_display_update, "Task_Display", 8192, NULL, 3, NULL);
    } else {
        ESP_LOGE("MAIN", "Fallo al crear las colas, semáforos o mutexes.");
    }
}
