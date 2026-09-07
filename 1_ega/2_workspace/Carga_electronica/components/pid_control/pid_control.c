#include "pid_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "../adc_sensor/include/adc_sensor.h"      // Para leer el struct del ADC
#include "../system_manager/include/system_manager.h"   // Para leer el struct de configuración

static const char *TAG = "PID_CEREBRO";

// --- CONSTANTES DEL PID (Sintonización inicial básica) ---
// Deberás calibrar estos números basándote en tu hardware real
#define KP      800.0f
#define KI      150.0f
#define KD      0.0f

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
    pid_config_t previous_cfg = current_cfg;

    ESP_LOGI(TAG, "Controlador PID listo y sincronizado con el ADC.");

    while (1) {
        // 1. Nos bloqueamos esperando una lectura NUEVA del ADC
        if (xQueueReceive(adc_queue, &current_readings, portMAX_DELAY) == pdTRUE) {
            
            // 2. Revisamos de paso si el usuario cambió el setpoint o modo (sin bloquear)
            xQueueReceive(pid_cfg_queue, &current_cfg, 0);

            if (current_cfg.mode != previous_cfg.mode ||
                current_cfg.setpoint != previous_cfg.setpoint ||
                current_cfg.force_stop != previous_cfg.force_stop) {
                integral = 0.0f;
                last_error = 0.0f;
                previous_cfg = current_cfg;
            }

            float target_current = 0.0f;

            // 3. LA LÓGICA DE NEGOCIO (CC vs CR)
            if (current_cfg.force_stop) {
                // ALARMA ACTIVA: Apagado de emergencia
                target_current = 0.0f;
                integral = 0.0f; // Resetear integral para no acumular error
            } else if (current_cfg.mode == MODE_CC) {
                // En modo Corriente Constante, el objetivo directo es el setpoint en Amperios
                target_current = current_cfg.setpoint;
            } else {
                // En modo Resistencia Constante aplicamos la Ley de Ohm: I = V / R
                // Evitamos la división por cero si la resistencia es menor a 1 Ohm o el voltaje es ínfimo
                if (current_cfg.setpoint >= 1.0f && current_readings.voltage_v > 0.1f) {
                    target_current = current_readings.voltage_v / current_cfg.setpoint;
                } else {
                    target_current = 0.0f;
                }
            }

            // 4. ALGORITMO PID
            error = target_current - current_readings.current_a; // Error = Deseo - Realidad

            // La salida solo puede aumentar la conducción. Si ya hay mas corriente
            // que la solicitada, retirar inmediatamente toda la orden acumulada.
            if (error <= 0.0f || current_cfg.force_stop) {
                integral = 0.0f;
                derivative = 0.0f;
                last_error = error;
                output = 0.0f;
            } else {
                integral += error * dt;
                if (integral > 4095.0f) integral = 4095.0f;

                derivative = (error - last_error) / dt;
                last_error = error;
                output = (error * KP) + (integral * KI) + (derivative * KD);
            }
            
            // Acotamos la salida al rango físico real del DAC (0V a 3.3V mapeado en 12 bits)
            if (output > 4095.0f) output = 4095.0f;
            if (output < 0.0f) output = 0.0f;

            // 5. Despachamos el resultado al DAC
            uint16_t dac_output_digital = (uint16_t)output;
            xQueueSend(dac_queue, &dac_output_digital, 0);

            // Registro de telemetría de control para debugging de precisión
            ESP_LOGI(TAG, "Target: %.2f A | Real: %.2f A | Error: %.2f | DAC Out: %d",
                     target_current, current_readings.current_a, error, dac_output_digital);
        }
    }
}