#include "mcp4725.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h" 
#include "esp_log.h"

static const char *TAG = "DAC";

#define MCP4725_ADDR        0x60 

extern QueueHandle_t dac_queue;
extern i2c_master_bus_handle_t i2c_bus_handle; // Importamos el bus creado en main.c

void task_dac_update(void *pvParameters) {
    uint16_t dac_value = 0;
    uint8_t data_buffer[2];

    // 1. Enlazamos este dispositivo específico al bus global
    i2c_master_dev_handle_t dac_handle = NULL;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP4725_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &dac_handle));

    ESP_LOGI(TAG, "Tarea del DAC Iniciada: Esperando datos");

    while (1) {
        if (xQueueReceive(dac_queue, &dac_value, portMAX_DELAY) == pdTRUE) {
            
            if (dac_value > 4095) dac_value = 4095;

            data_buffer[0] = (uint8_t)((dac_value >> 8) & 0x0F);
            data_buffer[1] = (uint8_t)(dac_value & 0xFF);

            // 2. Transmisión directa y segura. El driver gestiona las colisiones de hilos.
            esp_err_t err = i2c_master_transmit(dac_handle, data_buffer, sizeof(data_buffer), pdMS_TO_TICKS(50));

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Error de escritura I2C: %s", esp_err_to_name(err));
            }
        }
    }
}