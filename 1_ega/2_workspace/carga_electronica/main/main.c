#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// Componentes
#include "../components/mcp4725/include/mcp4725.h"
#include "../components/encoder/include/encoder.h"
#include "../components/eeprom/include/eeprom.h"
#include "../components/adc_sensor/include/adc_sensor.h"
#include "../components/system_manager/include/system_manager.h"
#include "../components/pid_control/include/pid_control.h"
#include "../components/display_tft/include/display_tft.h"

// --- DEFINICIONES DE I2C ---
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_SDA_IO           8        
#define I2C_MASTER_SCL_IO           9
#define I2C_MASTER_FREQ_HZ          100000   // 100kHz

// Declaración global de las vías de comunicación
QueueHandle_t adc_queue = NULL;
QueueHandle_t encoder_queue = NULL;
SemaphoreHandle_t button_sem = NULL;
QueueHandle_t pid_cfg_queue = NULL; // Envía el modo/setpoint del SysManager al PID
QueueHandle_t dac_queue = NULL;    // Envía el valor de 12 bits del PID al DAC
SemaphoreHandle_t i2c_mutex = NULL; // mutex para que la EEPROM y el DAC no choquen
QueueHandle_t display_queue = NULL;

// Handle global del Bus I2C para que lo usen todos los componentes
i2c_master_bus_handle_t i2c_bus_handle = NULL; 

// Inicializador del bus v6.0
static esp_err_t i2c_master_init(void) {
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
}

void app_main(void) {

    // Inicializo
    if (i2c_master_init() == ESP_OK) {
        ESP_LOGI("MAIN", "Bus I2C inicializado con éxito.");
    } else {
        ESP_LOGE("MAIN", "Fallo crítico al inicializar I2C. Sistema detenido.");
        return;
    }

    // creo recursos de FreeRTOS
    adc_queue       = xQueueCreate(1, sizeof(sensor_data_t)); 
    encoder_queue   = xQueueCreate(10, sizeof(int));
    button_sem      = xSemaphoreCreateBinary();
    pid_cfg_queue   = xQueueCreate(1, sizeof(pid_config_t));  
    dac_queue       = xQueueCreate(2, sizeof(uint16_t));
    i2c_mutex       = xSemaphoreCreateMutex(); 

    // Creación de la cola para transferir datos a la pantalla
    display_queue   = xQueueCreate(30, sizeof(ui_update_t));

    // Agregamos 'display_queue' a la validación de seguridad
    if (adc_queue && encoder_queue && button_sem && pid_cfg_queue && dac_queue && i2c_mutex && display_queue) {
        
        // Lanzamiento de Tareas
        
        // --- Tarea del Display ---
        // stack de 8192 (8KB) requerido por la asignación de memoria pesada de LVGL. 
        // Prioridad 3 (Baja-Media) para no interrumpir el lazo PID de potencia.
        xTaskCreate(task_display_update, "Task_Display", 8192, NULL, 3, NULL);

        // Control y Potencia (alta prioridad)
        xTaskCreate(task_encoder_read,   "Task_Encoder", 3072, NULL, 6, NULL); 
        xTaskCreate(task_adc_read,       "Task_ADC",     4096, NULL, 5, NULL); 
        xTaskCreate(task_pid_compute,    "Task_PID",     4096, NULL, 5, NULL); 
        xTaskCreate(task_dac_update,     "Task_DAC",     3072, NULL, 5, NULL); 
        xTaskCreate(task_system_manager, "Task_SysMan",  4096, NULL, 4, NULL); 
        
    } else {
        ESP_LOGE("MAIN", "Fallo al crear queue, semaforo o mutex");
    }
}
