#include <stdio.h>
#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "FreeRTOS.h"
#include "timers.h"

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
 * @brief Auto reload Timer para conmutar el LED
 * @param timer handle del Timer programado
 */
void soft_timer_blinky(TimerHandle_t timer) {
#ifdef CYW43_WL_GPIO_LED_PIN
    // Pide al chip de WiFi que conmute el LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN));
#else
    // Conmuta el LED
    gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
#endif
}

/**
 * @brief One shot Timer para mandar mensaje por consola
 * @param timer handle del Timer programado
 */
void soft_timer_print(TimerHandle_t timer) {
    // Manda el mensaje por consola
    puts("One shot Timer disparado!");
}

// Task Control Blocks para los timers
StaticTimer_t blinky_tcb, print_tcb;

/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();

#ifdef CYW43_WL_GPIO_LED_PIN
    // Inicialización de chip de WiFi
    cyw43_arch_init();
#else
    // Inicialización de GPIO
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
#endif

    // Creación de Software Timers
    TimerHandle_t stimer_blinky = xTimerCreateStatic("Blinky Timer", pdMS_TO_TICKS(500), true, 0, soft_timer_blinky, &blinky_tcb);
    TimerHandle_t stimer_print = xTimerCreateStatic("Print Timer", pdMS_TO_TICKS(2000), false, 0, soft_timer_print, &print_tcb);

    // Inicia los Timers sin demora
    xTimerStart(stimer_blinky, 0);
    xTimerStart(stimer_print, 0);

    vTaskStartScheduler();
    while(true);
}
