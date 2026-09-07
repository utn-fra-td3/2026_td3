#include "mcp4725.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "DAC_HARDWARE";

#define MCP4725_ADDR        0x60 // Cambiar a 0x61 o 0x62 si tu escáner dio otro número
#define I2C_MASTER_NUM      I2C_NUM_0

extern QueueHandle_t dac_queue;
extern SemaphoreHandle_t i2c_mutex;

void task_dac_update(void *pvParameters) {
    uint16_t dac_value = 0;
    uint8_t data_buffer[2];

    ESP_LOGI(TAG, "Tarea del DAC Iniciada (Esperando datos de la cola)...");

    while (1) {
        // La tarea duerme profundamente aquí hasta que el PID calcula algo nuevo
        if (xQueueReceive(dac_queue, &dac_value, portMAX_DELAY) == pdTRUE) {
            
            // Protección por software: el DAC es de 12 bits max (0-4095)
            if (dac_value > 4095) dac_value = 4095;

            // Preparar los bytes para el "Fast Mode" del MCP4725
            data_buffer[0] = (uint8_t)((dac_value >> 8) & 0x0F);
            data_buffer[1] = (uint8_t)(dac_value & 0xFF);

            // SECCIÓN CRÍTICA: Tomamos el candado del I2C para que nadie nos interrumpa
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                
                esp_err_t err = i2c_master_write_to_device(
                    I2C_MASTER_NUM,
                    MCP4725_ADDR,
                    data_buffer,
                    sizeof(data_buffer),
                    pdMS_TO_TICKS(50) // Timeout corto de 5ms
                );

                // Soltamos el candado inmediatamente al terminar la transmisión
                xSemaphoreGive(i2c_mutex);

                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Error de escritura I2C: %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGE(TAG, "No se pudo tomar el Mutex I2C (Bus ocupado por otro componente)");
            }
        }
    }
}