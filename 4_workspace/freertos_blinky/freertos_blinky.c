#include <stdio.h>
#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief Tarea de blinky de LED
 */
void task_blinky(void *params) {
    
    while(1) {
    #ifdef CYW43_WL_GPIO_LED_PIN
        // Le pido al chip de WiFi que conmute el GPIO 
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN));
    #else
        // Toggle GPIO
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    #endif
        // Demora de ticks equivalentes a 500 ms
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();

#ifdef CYW43_WL_GPIO_LED_PIN
    // Para Pico W, se habilita el chip de WiFi que controla el LED
    cyw43_arch_init();
#else
    // Inicializacion de GPIO
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif

    // Creacion de tareas
    xTaskCreate(task_blinky, "Blinky", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el scheduler
    vTaskStartScheduler();
    while(1);
}