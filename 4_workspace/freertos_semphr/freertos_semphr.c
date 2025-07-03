#include <stdio.h>
#include "pico/stdlib.h"

// Inclusión de biblioteca del chip para Pico W
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "FreeRTOS.h"
#include "semphr.h"

// GPIO de boton
#define SEMPHR_BTN  20

// Semaforo para desbloquear tarea
SemaphoreHandle_t semphr;

/**
 * @brief Tarea que maneja el semaforo
 */
void task_semphr(void *params) {

    while(1) {
        // Entrega el semaforo al apretar el boton
        if(!gpio_get(SEMPHR_BTN)) {
            xSemaphoreGive(semphr);
        }
        // Bloqueo para dar lugar a otra tarea
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea que maneja el LED
 */
void task_led(void *params) {

    while(1) {
        // Intenta tomar el semaforo, se bloquea si no esta disponible
        xSemaphoreTake(semphr, portMAX_DELAY);
    
    #ifdef CYW43_WL_GPIO_LED_PIN
        // Conmuta el LED desde el chip
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, !cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN));
    #else
        // Conmuta el LED
        gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
    #endif
    }
}

/**
 * @brief Programa principal
 */
int main(void) {
    stdio_init_all();

    // Inicializacion de GPIO para pulsador
    gpio_init(SEMPHR_BTN);
    gpio_set_dir(SEMPHR_BTN, false);
    gpio_pull_up(SEMPHR_BTN);

#ifdef CYW43_WL_GPIO_LED_PIN
    // Inicialización del chip de WiFi
    cyw43_arch_init();
#else
    // Inicializacion de GPIO para LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
#endif

    // Inicializacion de semaforo y lo tomo para bloquar
    vSemaphoreCreateBinary(semphr);
    xSemaphoreTake(semphr, portMAX_DELAY);

    // Creacion de tareas
    xTaskCreate(task_semphr, "Semphr", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_led, "LED", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Inicia el sistema operativo
    vTaskStartScheduler();
    while (true);
}
