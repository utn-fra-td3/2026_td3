#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "esp_log.h"

#include "../components/mcp4725/include/mcp4725.h"

// --- CONFIGURACIÓN DE PINES I2C ---
#define I2C_MASTER_SDA_IO           8      // Pin para datos (SDA)
#define I2C_MASTER_SCL_IO           9      // Pin para reloj (SCL)

#define I2C_MASTER_NUM              I2C_NUM_0 // Usaremos el puerto I2C 0
#define I2C_MASTER_FREQ_HZ          400000    // Frecuencia a 400kHz 
#define I2C_MASTER_TX_BUF_DISABLE   0         // El maestro no usa buffer TX
#define I2C_MASTER_RX_BUF_DISABLE   0         // El maestro no usa buffer RX


static const char *TAG = "MAIN";

// Handle de la cola de FreeRTOS
static QueueHandle_t dac_queue = NULL;

/**
 * @brief Inicializa el puerto I2C en modo Maestro
 */
esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, // Activa resistencias pull-up internas
        .scl_pullup_en = GPIO_PULLUP_ENABLE, // Activa resistencias pull-up internas
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    // 1. Configurar los parámetros estructurales
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }

    // 2. Instalar el driver (Encender el bus de verdad)
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 
                              I2C_MASTER_RX_BUF_DISABLE, 
                              I2C_MASTER_TX_BUF_DISABLE, 0);
}

/**
 * @brief Tarea 1: Control del DAC
 * Tiene mayor prioridad porque el control de la carga es crítico.
 */
void task_dac_control(void *pvParameters) {

    //ACA HARDCODEO para prueba
    uint16_t dac_target_value = 512; 

    ESP_LOGI(TAG, "Iniciando Tarea de Control del DAC...");

    while (1) {
        // Escribo el valor en el dac
        mcp4725_set_voltage(dac_target_value);

        // 2. Enviar el valor a la cola
        // máximo 10ms si la cola estuviera llena (pdMS_TO_TICKS)
        if (xQueueSend(dac_queue, &dac_target_value, pdMS_TO_TICKS(10)) != pdPASS) {
            ESP_LOGW(TAG, "Cola llena. No se pudo enviar el dato a la consola.");
        }

        // El control no necesita ejecutarse continuamente. Pasamos a estado "Blocked" por 1 segundo para liberar el CPU.
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

/**
 * @brief Tarea 2: Impresión por Consola (Logger)
 * Tiene menor prioridad. La telemetría no debe retrasar el control.
 */
void task_console_logger(void *pvParameters) {
    uint16_t received_value = 0;
    ESP_LOGI(TAG, "Iniciando Tarea de Consola...");

    while (1) {
        // xQueueReceive bloquea la tarea indefinidamente (portMAX_DELAY).
        // Esta tarea consume 0% de CPU hasta que la Tarea 1 pone un dato en la cola.
        if (xQueueReceive(dac_queue, &received_value, portMAX_DELAY) == pdTRUE) {
            
            // Calculamos el voltaje teórico para mostrarlo en consola
            float voltage = (received_value * 3.3f) / 4096.0f;

            ESP_LOGI(TAG, "DAC UPDATE -> Digital: %d | Voltaje aprox: %.2f V", 
                     received_value, voltage);
        }
    }
}

void app_main(void) {
    ESP_LOGI("MAIN", "Iniciando Carga Electrónica...");

    // 1. Inicializar el hardware (Bus I2C)
    esp_err_t err = i2c_master_init();
    if (err == ESP_OK) {
        ESP_LOGI("MAIN", "Bus I2C inicializado correctamente");
    } else {
        ESP_LOGE("MAIN", "Error inicializando I2C: %s", esp_err_to_name(err));
        return; // Si el I2C falla, detenemos el programa acá

    // 2. Crear la cola de comunicación
    dac_queue = xQueueCreate(5, sizeof(uint16_t));

    // 3. Crear las tareas de FreeRTOS
    if (dac_queue != NULL) {
        xTaskCreate(task_dac_control, "DAC_Control", 3072, NULL, 5, NULL);
        xTaskCreate(task_console_logger, "Console_Logger", 3072, NULL, 4, NULL);
    }
}