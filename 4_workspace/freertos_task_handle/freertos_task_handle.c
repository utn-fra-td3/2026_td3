#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

// GPIO de botones
#define UP_BTN  20
#define DW_BTN  21
#define DEL_BTN 22

// Handles de tareas para cambiar prioridades
TaskHandle_t task_1, task_2;

/**
 * @brief Tarea que gestiona los botones
 */
void task_btn(void *params) {

    while(1) {
        // Chequeo los botones
        if(!gpio_get(UP_BTN)) {
            // Sube la prioridad de la tarea 1
            int32_t prio = uxTaskPriorityGet(task_1);
            vTaskPrioritySet(task_1, (prio + 1 < configMAX_PRIORITIES)? prio + 1 : prio);
        }
        else if(!gpio_get(DW_BTN)) {
            // Bajo la prioridad de la tarea 1
            int32_t prio = uxTaskPriorityGet(task_1);
            vTaskPrioritySet(task_1, (prio < 2)? 1 : prio - 1);
        }
        else if(!gpio_get(DEL_BTN)) {
            // Elimina ambas tareas y limpia los handle
            vTaskDelete(task_1);
            vTaskDelete(task_2);
            puts("Tareas eliminadas");
        }
        // Bloqueo para dar espacio a otras tareas
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea que imprime sin bloquear un mensaje
 * por consola
 */
void task_msg(void *params) {
    // Obtengo el id y handle
    uint8_t id = *(uint8_t*)params;
    TaskHandle_t handle = (id == 1)? task_1 : task_2;

    while(1) {
        printf("Tarea %d con prioridad %d\n", id, uxTaskPriorityGet(handle));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();

    // Inicializacion de entradas
    gpio_init(UP_BTN);
    gpio_init(DW_BTN);
    gpio_init(DEL_BTN);
    gpio_set_dir(UP_BTN, false);
    gpio_set_dir(DW_BTN, false);
    gpio_set_dir(DEL_BTN, false);
    gpio_pull_up(UP_BTN);
    gpio_pull_up(DW_BTN);
    gpio_pull_up(DEL_BTN);    

    const uint8_t id_1 = 1, id_2 = 2;
    // Creacion de tareas
    xTaskCreate(task_btn, "Btn", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_msg, "Msg1", 2 * configMINIMAL_STACK_SIZE, (void*)&id_1, 2, &task_1);
    xTaskCreate(task_msg, "Msg2", 2 * configMINIMAL_STACK_SIZE, (void*)&id_2, 2, &task_2);

    // Incicia el sistema operativo
    vTaskStartScheduler();
    while(1);
}
