#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/**
 * @brief Encargada de reservar la memoria necesaria para la tarea de servicio de timers
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.4.2.1
 * 
 * @param ppxTimerTaskTCBBuffer referencia al Task Control Block del Tmr Svc
 * @param ppxTimerTaskStackBuffer referencia al stack del Tmr Svc
 * @param cantidad de palabras del stack del Tmr Svc
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
    // Task Control Block de la tarea
    static StaticTask_t xTimerTaskTCB;
    // Se crea un array con el tamaño del stack
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    // Se asignan las referencias a los parámetros
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/**
 * @brief Encargada de reservar la memoria necesaria para la tarea IDLE
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.4.2.2
 * 
 * @param ppxIdleTaskTCBBufer referencia al Task Control Block de la tarea IDLE
 * @param ppxIdleTaskStackBuffer referencia al stack de la tarea IDLE
 * @param pulIdleTaskStackSize cantidad de palabras del stack de la tarea IDLE
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize ) {
    // Task Control Block de la tarea
    static StaticTask_t xIdleTaskTCB;
    // Se crea un array con el tamaño del stack
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    // Se asignan las referencias a los parámetros
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

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
    // Estructura para la cola
    sensor_data_t data = {0};

    while(1) {
        // Leo el sensor y preparo los datos para la cola
        data.raw = adc_read();
        data.voltage = data.raw * 3.3 / (1 << 12);
        // El calculo de temperatura sale de la documentacion del SDK
        data.temperature = 27 - (data.voltage - 0.706) / 0.001721;
        // Escribo la cola con siempre el ultimo valor
        xQueueOverwrite(queue_sensor, &data);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Stacks y TCB de tareas

// Tamaño de stack de la tarea
#define ADC_STACK_SIZE      128
#define PRINT_STACK_SIZE    256

// Stacks
StackType_t print_stack[PRINT_STACK_SIZE]; 
StackType_t adc_stack[ADC_STACK_SIZE];

// Task Control Blocks
StaticTask_t print_tcb;
StaticTask_t adc_tcb;

// Variables para la cola

// Cantidad de elementos de la cola
#define QUEUE_LENGTH    1
// Tamaño de cada item de la cola
#define QUEUE_ITEM_SIZE sizeof(sensor_data_t)

// Información de la cola
StaticQueue_t squeue_sensor;
// Almacenamiento de la cola
uint8_t squeue_storage[QUEUE_LENGTH * QUEUE_ITEM_SIZE];

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();

    // Inicializacion del ADC y sensor de temperatura
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);

    // Inicializacion de cola estática para estructura
    queue_sensor = xQueueCreateStatic(QUEUE_LENGTH, QUEUE_ITEM_SIZE, squeue_storage, &squeue_sensor);

    // Creacion de tareas
    xTaskCreateStatic(task_print, "Print", PRINT_STACK_SIZE, NULL, 2, print_stack, &print_tcb);
    xTaskCreateStatic(task_adc, "ADC", ADC_STACK_SIZE, NULL, 1, adc_stack, &adc_tcb);
    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(true);
}
