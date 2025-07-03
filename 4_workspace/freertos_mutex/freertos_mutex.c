#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// Mutex para sincronizacion de tareas
SemaphoreHandle_t mutex;

/**
 * @brief Tarea que usa vTaskDelay e imprime en 
 * consola la cantidad de ticks
 */
void task_delay(void *params) {
    // Cantidad de ticks al iniciar la tarea
    TickType_t ticks = xTaskGetTickCount();

    while(1) {
        // Bloqueo e intento escribir si el mutex lo permite
        vTaskDelay(pdMS_TO_TICKS(500));
        xSemaphoreTake(mutex, portMAX_DELAY);
        printf("Con vTaskDelay se demoro %d ticks\n", xTaskGetTickCount() - ticks);
        xSemaphoreGive(mutex);
        // Actualizo la cantidad de ticks
        ticks = xTaskGetTickCount();
    }
}

/**
 * @brief Tarea que usa vTaskDelayUntil e imprime 
 * en consola la cantidad de ticks
 */
void task_delay_until(void *params) {
    // Cantidad de ticks al iniciar la tarea
    TickType_t ticks = xTaskGetTickCount();

    while(1) {
        // Ticks antes de actualizar
        TickType_t prev_ticks = ticks;
        // Bloqueo e intento escribir si el mutex lo permite
        xTaskDelayUntil(&ticks, pdMS_TO_TICKS(500));
        xSemaphoreTake(mutex, portMAX_DELAY);
        printf("Con vTaskDelayUntill se demoro %d ticks\n", ticks - prev_ticks);
        xSemaphoreGive(mutex);
    }
}


/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();

    // Inicializo mutex
    mutex = xSemaphoreCreateMutex();

    // Creacion de tareas
    xTaskCreate(task_delay, "Delay", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_delay_until, "DelayUntil", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(1);
}
