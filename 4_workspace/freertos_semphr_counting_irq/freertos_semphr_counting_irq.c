#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// GPIOs a usar como entrada y salida
#define APP_IN  14
#define APP_OUT 15

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT   10000

// Semaforo counting
SemaphoreHandle_t semphr_counting;

/**
 * @brief Callback para interrupcion por GPIO
 */
void irq_callback(uint gpio, uint32_t event_mask) {
    // Incremento la cuenta
    BaseType_t to_higher_priority_task = false;
    xSemaphoreGiveFromISR(semphr_counting, &to_higher_priority_task);
    // Reviso si es necesario el cambio a otra tarea
    portYIELD_FROM_ISR(to_higher_priority_task);
}

/**
 * @brief Tarea que manda la cuenta final por consola y 
 * limpia el semaforo
 */
void task_clear(void *params) {

    while(1) {
        // Mando por consola y limpio semaforo
        printf("Se contaron %d pulsos en 1 segundo\n", uxSemaphoreGetCount(semphr_counting));
        xQueueReset(semphr_counting);
        // Bloqueo por un segundo para contar
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Tarea que conmuta una salida para realimentar
 * en el pin de entrada con interrupcion
 */
void task_out(void *params) {
    // Aseguro que sea consistente el bloqueo
    TickType_t tick = xTaskGetTickCount();

    while(1) {
        // Conmuto salida y bloqueo por 10 ms
        gpio_put(APP_OUT, !gpio_get(APP_OUT));
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();

    // Inicializacion de GPIO
    gpio_init(APP_IN);
    gpio_init(APP_OUT);
    gpio_set_dir(APP_IN, false);
    gpio_set_dir(APP_OUT, true);
    // Agrego interrupcion por flanco descendente
    gpio_set_irq_enabled_with_callback(APP_IN, GPIO_IRQ_EDGE_FALL, true, irq_callback);
    // Creo semaforo
    semphr_counting = xSemaphoreCreateCounting(MAX_COUNT, 0);

    // Creacion de tareas
    xTaskCreate(task_clear, "Clr", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_out, "Out", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(1);
}
