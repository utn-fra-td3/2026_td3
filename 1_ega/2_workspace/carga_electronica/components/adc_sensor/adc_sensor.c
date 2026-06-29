#include "adc_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "../display_tft/include/display_tft.h"

static const char *TAG = "ADC_SENSOR";

// --- CONFIGURACIÓN DE PINES Y CANALES ---
#define ADC_UNIT            ADC_UNIT_1
#define ADC_VOLTAGE_CHAN    ADC_CHANNEL_0 
#define ADC_CURRENT_CHAN    ADC_CHANNEL_1 

// Factores de escala
#define SCALE_VOLTAGE       1.0f 
#define SCALE_CURRENT       1.0f 

// Cola de datos adc
extern QueueHandle_t adc_queue;

// Cola de datos del display
extern QueueHandle_t display_queue;

// Variables globales
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t cali_handle = NULL;
static bool cali_enabled = false;

// --- 1. FUNCIÓN PRIVADA DE INICIALIZACIÓN ---
static void adc_init_hardware(void) {
    ESP_LOGI(TAG, "Inicializando ADC1...");

    // Configuramos la unidad ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    // Configuramos los canales (Atenuación de 12dB para poder leer de 0V a 3.1V aprox)
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_VOLTAGE_CHAN, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CURRENT_CHAN, &config));

    // Intentamos cargar la calibración de fábrica
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        cali_enabled = true;
        ESP_LOGI(TAG, "Calibración habilitada.");
    } else {
        ESP_LOGW(TAG, "No se pudo cargar la calibración. Usando valores sin calibracion");
    }
}

// --- 2. TAREA PRINCIPAL DE FREERTOS ---
void task_adc_read(void *pvParameters) {
    // Inicializamos el hardware ADC
    adc_init_hardware();

    // Variables para las lecturas
    int raw_voltage, raw_current;
    int mv_voltage = 0, mv_current = 0;
    
    // Variables para el filtro matemático
    float filtered_voltage = 0.0f;
    float filtered_current = 0.0f;
    const float alpha = 0.1f; // aplico tecnica de 10% lectura nueva y 90% lectura anterior

    ESP_LOGI(TAG, "Tarea de lectura de sensores iniciada.");

    while (1) {
        // Leemos los valores del hardware
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_VOLTAGE_CHAN, &raw_voltage));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CURRENT_CHAN, &raw_current));

        // Convertimos a milivoltios reales
        if (cali_enabled) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_voltage, &mv_voltage));
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_current, &mv_current));
        } else {
            // Si no hay calibración, hacemos una estimación
            mv_voltage = (raw_voltage * 3300) / 4095;
            mv_current = (raw_current * 3300) / 4095;
        }

        // Aplicamos el Filtro Promedio Móvil Exponencial
        filtered_voltage = (mv_voltage * alpha) + (filtered_voltage * (1.0f - alpha));
        filtered_current = (mv_current * alpha) + (filtered_current * (1.0f - alpha));

        // Aplicamos las escalas de nuestro hardware
        sensor_data_t final_data;
        final_data.voltage_v = (filtered_voltage / 1000.0f) * SCALE_VOLTAGE;
        final_data.current_a = (filtered_current / 1000.0f) * SCALE_CURRENT;

        // Imprimimos por consola para chequear
        ESP_LOGI(TAG, "Voltaje: %.2f V | Corriente: %.2f A", final_data.voltage_v, final_data.current_a);

        // --- ENVÍO AL PID (Lazo de Control) ---
        if (adc_queue != NULL) {
            // xQueueOverwrite siempre pisa el dato viejo, asegurando latencia mínima para el PID
            xQueueOverwrite(adc_queue, &final_data); 
        }

        // --- ENVÍO A LA PANTALLA ---
        if (display_queue != NULL) {
            ui_update_t disp_msg;
            disp_msg.source = UI_MSG_FROM_ADC; 
            disp_msg.voltage = final_data.voltage_v;
            disp_msg.current = final_data.current_a;
            
            // Enviamos a la cola gráfica. Timeout 0 para no frenar nunca el muestreo del ADC si la cola se llena.
            xQueueSend(display_queue, &disp_msg, 0);
        }

        // Dormimos la tarea estrictamente 50 milisegundos
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}