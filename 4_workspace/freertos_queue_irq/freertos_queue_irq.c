#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Cantidad de lecturas
#define SAMPLES 4

/**
 * @brief Estructura para pasar los datos del sensor
 */
typedef struct {
    uint16_t raw;
    float voltage;
    float temperature;
} sensor_data_t;

// Cola para datos del sensor
QueueHandle_t queue_sensor;

/**
 * @brief Handler para la interrupcion del ADC
 */
void adc_irq_handler(void) {
    // Variable para verificar la necesidad de un cambio tarea
    BaseType_t to_higher_priority_task = false;
    // Deshabilito la interrupcion y detengo el ADC
    adc_irq_set_enabled(false);
    adc_run(false);
    // Variable para calcular el promedio de muestras
    uint32_t raw = 0;
    for(uint8_t i = 0; i < SAMPLES; i++) { raw += adc_fifo_get(); }
    // Limpio el FIFO
    adc_fifo_drain();
    // Datos para la cola
    sensor_data_t data = { .raw = (uint16_t) (raw / SAMPLES) };
    data.voltage = data.raw * 3.3 / (1 << 12);
    // El calculo de temperatura sale de la documentacion del SDK
    data.temperature = 27 - (data.voltage - 0.706) / 0.001721;
    // Envio por cola
    xQueueOverwriteFromISR(queue_sensor, &data, &to_higher_priority_task);
    // Reviso si es necesario el cambio a otra tarea
    portYIELD_FROM_ISR(to_higher_priority_task);
}

/**
 * @brief Tarea que escribe por consola
 */
void task_print(void *params) {
    // Estructura para la cola
    sensor_data_t data = {0};

    while(1) {
        // Leo el ultimo valor que haya en la cola
        xQueuePeek(queue_sensor, &data, portMAX_DELAY);
        // Escribo los datos
        printf("ADC raw: 0x%03x\n", data.raw);
        printf("ADC voltage: %.2f V\n", data.voltage);
        printf("Temperature: %.2f C\n\n", data.temperature);
        // Bloqueo para no saturar la consola
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Tarea que lee el ADC
 */
void task_adc(void *params) {

    while(1) {
        // Habilito la interrupcion del ADC nuevamente y corro las conversiones
        adc_irq_set_enabled(true);
        adc_run(true);
        // Bloqueo la tarea por un tiempo
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();
    // Inicializacion de cola para estructura
    queue_sensor = xQueueCreate(1, sizeof(sensor_data_t));
    // Inicializacion del ADC y sensor de temperatura
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    // Inicializo la interrupcion del ADC y la cantidad de lecturas necesarias
    adc_fifo_setup(true, false, SAMPLES, false, false);
    adc_irq_set_enabled(true);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_irq_handler);
    irq_set_enabled(ADC_IRQ_FIFO, true);
    adc_run(true);

    // Creacion de tareas
    xTaskCreate(task_print, "Print", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_adc, "ADC", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(true);
}
