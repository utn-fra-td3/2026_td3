#include "pid_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "../adc_sensor/include/adc_sensor.h"      // Para leer el struct del ADC
#include "../system_manager/include/system_manager.h"   // Para leer el struct de configuración

static const char *TAG = "PID";

// --- CONSTANTES DEL PID ---
// Constantes P, I, D
#define KP      150.0f
#define KI      45.0f
#define KD      5.0f

extern QueueHandle_t adc_queue;
extern QueueHandle_t pid_cfg_queue;
extern QueueHandle_t dac_queue;

void task_pid_compute(void *pvParameters) {
    // Memoria del PID
    float error = 0.0f, last_error = 0.0f, integral = 0.0f, derivative = 0.0f;
    float output = 0.0f;
    const float dt = 0.050f; // Periodo de tiempo fijo de 50ms sincronizado con el ADC

    // Estado local copiado de la cola del System Manager
    pid_config_t current_cfg = {.mode = MODE_CC, .setpoint = 0.0f};
    sensor_data_t current_readings;

    ESP_LOGI(TAG, "Controlador PID listo y sincronizado con el ADC.");

    while (1) {
        // bloqueo esperando una lectura nueva del ADC
        if (xQueueReceive(adc_queue, &current_readings, portMAX_DELAY) == pdTRUE) {
            
            // Revisamos si cambió el setpoint o modo (sin bloquear)
            xQueueReceive(pid_cfg_queue, &current_cfg, 0);

            float target_current = 0.0f;

            // MODO CC o CR
            if (current_cfg.mode == MODE_CC) {
                target_current = current_cfg.setpoint;
            } else {
                // Modo Resistencia Constante, ley de ohm
                // Evitamos la división por cero si la resistencia es menor a 1 Ohm o el voltaje es ínfimo
                if (current_cfg.setpoint >= 1.0f && current_readings.voltage_v > 0.1f) {
                    target_current = current_readings.voltage_v / current_cfg.setpoint;
                } else {
                    target_current = 0.0f;
                }
            }

            // ALGORITMO PID
            error = target_current - current_readings.current_a;
            
            // limitador para que no crezca hasta el infinito
            integral += error * dt;
            if (integral > 4095.0f) integral = 4095.0f;
            if (integral < 0.0f) integral = 0.0f;

            // Término Derivativo
            derivative = (error - last_error) / dt;
            last_error = error;

            // Ecuación final
            output = (error * KP) + (integral * KI) + (derivative * KD);

            // Acotamos la salida al rango físico real del DAC
            if (output > 4095.0f) output = 4095.0f;
            if (output < 0.0f) output = 0.0f;

            // Resultado DAC
            uint16_t dac_output_digital = (uint16_t)output;
            xQueueSend(dac_queue, &dac_output_digital, 0);

            // Registro de control
            ESP_LOGD(TAG, "Target: %.2f A | Real: %.2f A | Error: %.2f | DAC Out: %d", 
                     target_current, current_readings.current_a, error, dac_output_digital);
        }
    }
}