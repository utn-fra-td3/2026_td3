#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// Cola para mandar numeros aleatorios
QueueHandle_t queue_random;

/**
 * @brief Tarea que genera un numero aleatorio y lo manda por una cola
 */
void task_random(void *params) {

    while(1) {
        // Genera un numero aleatorio (0 a 100)
        uint32_t random_number = rand() % 100;
        // Mando por cola
        xQueueSend(queue_random, &random_number, portMAX_DELAY);
        // Bloqueo tarea para no saturar
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Tarea que escribe por consola lo que llega de la cola
 */
void task_print(void *params) {

    while(1) {
        // Bloqueo tarea hasta que llegue el dato
        uint32_t random_number;
        xQueueReceive(queue_random, &random_number, portMAX_DELAY);
        // Escribo por consola
        printf("Numero aleatorio generado: %d\n", random_number);
    }
}

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();

    // Inicializa semilla para numeros pseudoaleatorios
    srand(xTaskGetTickCount());
    // Inicializa la cola
    queue_random = xQueueCreate(1, sizeof(uint32_t));

    // Creacion de tareas
    xTaskCreate(task_random, "RNG", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_print, "Print", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}
