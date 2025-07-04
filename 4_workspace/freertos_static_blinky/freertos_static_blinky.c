#include <stdio.h>
#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "FreeRTOS.h"
#include "task.h"

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
 * @brief Tarea que hace parpadear el LED
 */
void task_blinky(void *params) {

    while(1) {
    #ifdef CYW43_WL_GPIO_LED_PIN
        // Pide al chip de WiFi que conmute el LED
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN));
    #else
        // Conmuta el LED
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    #endif
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Cantidad de palabras del stack del blinky
#define BLINKY_STACK_SIZE   128

// Stack de la tarea
StackType_t blinky_stack[BLINKY_STACK_SIZE];
// TCB de la tarea
StaticTask_t blinky_tcb;

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();
    
    // Inicializacion del GPIO del LED
#ifdef CYW43_WL_GPIO_LED_PIN
    cyw43_arch_init();
#else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
#endif

    // Creación de tarea estática
    xTaskCreateStatic(task_blinky, "Blinky", BLINKY_STACK_SIZE, NULL, 1, blinky_stack, &blinky_tcb);
    // La tarea se creó correctamente
    vTaskStartScheduler();
    while(1);
}
