#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/queue.h"

// Colas de comunicación entre tareas de atencion al display y guardado en memoria.
QueueHandle_t queue_display; // Cola para display LCD, recibe valores ADC para mostrar
QueueHandle_t queue_nvs_cmd; // Cola para NVS, recibe comandos/valores a guardar en NVS
QueueHandle_t queue_uart_rx; // Cola para recibir datos por UART
QueueHandle_t queue_uart_tx; // Cola para enviar datos por UART
uint32_t uart_data_rx; // Variable para almacenar datos recibidos por UART
uint32_t uart_data_tx; // Variable para almacenar datos a enviar por UART

volatile uint32_t frecuencia = 1;            // Hz, configurable desde el menú
volatile uint32_t tiempo_de_muestreo = 1000; // ms, calculado a partir de frecuencia

void task_sweep (void *param) {
    // marca el inicio de la cuenta con xTaskGetTickCount() y luego se desbloquea ciclicamente por el vTaskDelayUntil()
    TickType_t timestamp = xTaskGetTickCount();
    ESP_LOGI("task_sweep", "timestamp @ %ld", pdTICKS_TO_MS(timestamp));

        // Configuración del ADC en modo oneshot
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    // Selección de canal (ejemplo: GPIO1 → ADC1_CHANNEL_0)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,   // hasta ~3.3V
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &config);

    // Configuración de calibración
    adc_cali_handle_t cali_handle;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);

        int adc_raw;
        int adc_calibrated;
        // Entra al loop de tarea task_sweep

        while(1){
        for(uint32_t i = 0; i<esp_random(); i++);
        ESP_LOGI("task_sweep" , "Apunto de llamar a vTaskDelayUntil por 1000 ms");

        // Realiza las cuentas
        if(tiempo_de_muestreo > 0 && tiempo_de_muestreo < 10000){ // Validamos que el tiempo de muestreo esté en un rango razonable
            vTaskDelayUntil(&timestamp, pdMS_TO_TICKS(tiempo_de_muestreo));
            ESP_LOGI("task_sweep", "Desbloqueado");
        } else {
            ESP_LOGW("task_sweep", "Tiempo de muestreo no válido (%lu ms), usando valor por defecto de 1000 ms", (unsigned long)tiempo_de_muestreo);
            vTaskDelayUntil(&timestamp, pdMS_TO_TICKS(1000));        
        ESP_LOGI("task_sweep", "Desbloqueado");
        }
        
        // leemos el ADC y aplicamos calibración
        
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &adc_raw);
        adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
        // adc_cali_scheme_curve_fitting_calibrate(cali_handle, adc_raw, &adc_calibrated);
        ESP_LOGI("task_sweep", "Valor ADC calibrado: %d", adc_calibrated);
    
        xQueueSend(queue_display, &adc_raw, portMAX_DELAY); // Enviamos el valor ADC sin calibrar al display para mostrarlo, en un caso real se podría enviar el valor calibrado o ambos
        xQueueSend(queue_nvs_cmd, &adc_raw, portMAX_DELAY); // Enviamos el valor ADC sin calibrar a la tarea de NVS para guardarlo, en un caso real se podría enviar el valor calibrado o ambos
        xQueueSend(queue_uart_tx, &adc_raw, portMAX_DELAY); // Enviamos el valor ADC sin calibrar a la tarea de UART para enviarlo, en un caso real se podría enviar el valor calibrado o ambos
        
        ESP_LOGI("task_sweep", "Valor insertado en colas: %d", adc_calibrated);
    }
    }



void task_adc (void *param) {
    while(1){
        // Simulación de lectura ADC
        int adc_value = esp_random() % 4096; // Simula un valor ADC de 12 bits
        ESP_LOGI("task_adc", "Valor ADC: %d", adc_value);
        vTaskDelay(pdMS_TO_TICKS(500)); // Simula tiempo de lectura
    }
}

void task_menu_config (void *param) {
    
    while(1){
        // Simulación de menú de configuración
        ESP_LOGI("task_menu_config", "Mostrando menú de configuración...");
        vTaskDelay(pdMS_TO_TICKS(2000)); // Simula tiempo de interacción
        ESP_LOGI("task_menu_config", "Menú de configuración actualizado.");
         if (frecuencia > 0) {
            tiempo_de_muestreo = 1000 / frecuencia; // Hz -> ms tiempo de muestreo en ms, configurable desde el menú
        }

        ESP_LOGI("task_menu_config", "Frecuencia: %lu Hz -> Tiempo de muestreo: %lu ms",
                 (unsigned long)frecuencia, (unsigned long)tiempo_de_muestreo);
        ESP_LOGI("task_menu_config", "Menú de configuración actualizado.");
    }
}


void task_nvs (void *param) {
    while(1){
        uint32_t nvs_cmd;
        if(xQueueReceive(queue_nvs_cmd, &nvs_cmd, portMAX_DELAY) == pdPASS){
            ESP_LOGI("task_nvs", "Recibido comando/valor para NVS: %lu", (unsigned long)nvs_cmd);
            // Aquí se implementaría la lógica para guardar en NVS
        }
    }
}

void task_lcd_display (void *param) {
    while(1){
        int adc_value;
        if(xQueueReceive(queue_display, &adc_value, portMAX_DELAY) == pdPASS){
            ESP_LOGI("task_lcd_display", "Valor ADC para mostrar: %d", adc_value);
            // Aquí se implementaría la lógica para actualizar el display
        }
    }
} 

void task_uart (void *param) {
    while(1){
        // Envia datos de por UART
        uint32_t uart_data_tx = 0x12345678; // Simula datos a enviar
        if(xQueueSend(queue_uart_tx, &uart_data_tx, portMAX_DELAY)){
            ESP_LOGI("task_uart", "Datos preparados para envío por UART: 0x%08X", uart_data_tx);
        } else {
            ESP_LOGE("task_uart", "Error al preparar datos para envío por UART");
        }
        ESP_LOGI("task_uart", "Enviando datos por UART...");
        vTaskDelay(pdMS_TO_TICKS(3000)); // Simula tiempo de transmisión
        ESP_LOGI("task_uart", "Datos enviados por UART.");
        // Recibe datos por UART (simulado)
        if(xQueueReceive(queue_uart_rx, &uart_data_rx, portMAX_DELAY) == pdPASS){
        ESP_LOGI("task_uart", "Recibiendo datos por UART...");
        vTaskDelay(pdMS_TO_TICKS(3000)); // Simula tiempo de recepción
        ESP_LOGI("task_uart", "Datos recibidos por UART.");
            }
            }
}

void app_main(void){   
    queue_display = xQueueCreate(10, sizeof(int));       // valores ADC para mostrar
    queue_nvs_cmd  = xQueueCreate(5, sizeof(uint32_t));  // comandos/valores a guardar en NVS

    if (queue_display == NULL || queue_nvs_cmd == NULL) {
        ESP_LOGE("app_main", "Error al crear las colas");
    }
    // Inicializacion de tarea task_sweep
    xTaskCreate(task_sweep, "task_Sweep", 2048, NULL, 1, NULL);

    // Inicializacion de tarea task_adc
    xTaskCreate(task_adc, "task_ADC", 2048, NULL, 1, NULL);

    // Inicializacion de tarea task_menu_config
    xTaskCreate(task_menu_config, "task_Menu_Config", 2048, NULL, 1, NULL);

    // Inicializacion de tarea task_nvs
    xTaskCreate(task_nvs, "task_NVS", 2048, NULL, 1, NULL);

    // Inicializacion de tarea task_lcd_display
    xTaskCreate(task_lcd_display, "task_LCD_Display", 2048, NULL, 1, NULL);

    // Inicializacion de tarea task_uart
    xTaskCreate(task_uart, "task_UART", 2048, NULL, 1, NULL);

}
