#include "adc_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "../display_tft/include/display_tft.h" // Para poder enviar los datos al display

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "pin_setting.h"

static const char *TAG = "ADC_SENSOR";

// Puntos de calibracion: tension medida en el pin ADC y valor real.
static const float voltage_adc_mv[] = {60.0f, 360.0f, 610.0f, 850.0f, 1170.0f, 1360.0f, 1510.0f};
static const float voltage_real_v[] = {0.0f, 0.5f, 1.5f, 2.5f, 4.5f, 6.5f, 9.5f};

// Puntos de calibracion: Vsense medida en el pin ADC y corriente real.
static const float current_adc_mv[] = {80.0f, 610.0f, 1080.0f, 1620.0f, 2070.0f};
static const float current_real_a[] = {0.0f, 0.035f, 0.085f, 0.135f, 0.185f};

static float interpolate_calibration(float adc_mv, const float *adc_points,
                                     const float *real_points, size_t point_count)
{
    if (adc_mv <= adc_points[0]) {
        return real_points[0];
    }
    for (size_t index = 1; index < point_count; index++) {
        if (adc_mv <= adc_points[index]) {
            float adc_span = adc_points[index] - adc_points[index - 1];
            float real_span = real_points[index] - real_points[index - 1];
            float fraction = (adc_mv - adc_points[index - 1]) / adc_span;
            return real_points[index - 1] + fraction * real_span;
        }
    }

    // Extrapola el ultimo tramo para no ocultar sobrecorrientes fuera de tabla.
    float adc_span = adc_points[point_count - 1] - adc_points[point_count - 2];
    float real_span = real_points[point_count - 1] - real_points[point_count - 2];
    float fraction = (adc_mv - adc_points[point_count - 2]) / adc_span;
    return real_points[point_count - 2] + fraction * real_span;
}

// Cola de datos para enviar las mediciones a otras tareas
extern QueueHandle_t adc_queue;

extern QueueHandle_t display_queue;

// Variables globales estáticas (privadas de este archivo)
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
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12 bits (0 a 4095)
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_VOLTAGE_CHAN, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CURRENT_CHAN, &config));

    // Intentamos cargar la calibración de fábrica (Curve Fitting)
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        cali_enabled = true;
        ESP_LOGI(TAG, "Calibración por hardware habilitada.");
    } else {
        ESP_LOGW(TAG, "No se pudo cargar la calibración. Usando valores crudos.");
    }
}

// --- 2. TAREA PRINCIPAL DE FREERTOS ---
void task_adc_read(void *pvParameters) {
    // 1. Inicializamos el hardware antes de entrar al bucle infinito
    adc_init_hardware();

    // Variables para las lecturas
    int raw_voltage, raw_current;
    int mv_voltage = 0, mv_current = 0;
    
    // Variables para el filtro matemático (Inician en cero)
    float filtered_voltage = 0.0f;
    float filtered_current = 0.0f;
    const float alpha = 0.1f; // Peso del filtro: 10% lectura nueva, 90% lectura vieja

    ESP_LOGI(TAG, "Tarea de lectura de sensores iniciada.");

    while (1) {
        // A. Leemos los valores crudos del hardware
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_VOLTAGE_CHAN, &raw_voltage));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CURRENT_CHAN, &raw_current));

        // B. Convertimos a milivoltios reales usando la calibración de Espressif
        if (cali_enabled) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_voltage, &mv_voltage));
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_current, &mv_current));
        } else {
            // Si no hay calibración, hacemos una estimación burda
            mv_voltage = (raw_voltage * 3300) / 4095;
            mv_current = (raw_current * 3300) / 4095;
        }

        // C. Aplicamos el Filtro Promedio Móvil Exponencial (EMA)
        filtered_voltage = (mv_voltage * alpha) + (filtered_voltage * (1.0f - alpha));
        filtered_current = (mv_current * alpha) + (filtered_current * (1.0f - alpha));

        // D. Aplicamos las escalas de nuestro hardware
        sensor_data_t final_data;
        final_data.voltage_v = interpolate_calibration(
            filtered_voltage, voltage_adc_mv, voltage_real_v,
            sizeof(voltage_adc_mv) / sizeof(voltage_adc_mv[0]));
        final_data.current_a = interpolate_calibration(
            filtered_current, current_adc_mv, current_real_a,
            sizeof(current_adc_mv) / sizeof(current_adc_mv[0]));


        // E. Imprimimos por consola para Diagnóstico (Opcional, puedes comentarlo luego si hace mucho "spam")
        ESP_LOGI(TAG,
             "ADC V raw=%d (%d mV) -> %.2f V | ADC I raw=%d (%d mV) -> %.2f A",
             raw_voltage, mv_voltage, final_data.voltage_v,
             raw_current, mv_current, final_data.current_a);

        // --- 1. ENVÍO AL PID (Lazo de Control) ---
        if (adc_queue != NULL) {
            // xQueueOverwrite siempre pisa el dato viejo, asegurando latencia mínima para el PID
            xQueueOverwrite(adc_queue, &final_data); 
        }

        // --- 2. NUEVO: ENVÍO A LA PANTALLA (Interfaz Gráfica) ---
        if (display_queue != NULL) {
            ui_update_t disp_msg;
            disp_msg.source = UI_MSG_FROM_ADC; // Identificador vital: ¡Soy el ADC!
            disp_msg.voltage = final_data.voltage_v;
            disp_msg.current = final_data.current_a;
            // No inicializamos 'mode' ni 'setpoint' porque la pantalla las ignorará gracias al 'source'
            
            // Enviamos a la cola gráfica. Timeout 0 para no frenar nunca el muestreo del ADC si la cola se llena.
            xQueueSend(display_queue, &disp_msg, 0);
        }

        // --- 3. ENVÍO AL SYSTEM MANAGER (Para Alarmas OVP/OCP) ---
        extern QueueHandle_t sysman_queue;
        if (sysman_queue != NULL) {
            sysman_msg_t sys_msg;
            sys_msg.type = SYSMAN_MSG_ADC_UPDATE;
            sys_msg.data.adc.voltage = final_data.voltage_v;
            sys_msg.data.adc.current = final_data.current_a;
            xQueueSend(sysman_queue, &sys_msg, 0);
        }

        // F. Dormimos la tarea estrictamente 50 milisegundos (20 lecturas por segundo)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}