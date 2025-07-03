#include <stdio.h>
#include "pico/stdlib.h"

// Incluyo condicionalmente la biblioteca para el chip de WiFi
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#include "FreeRTOS.h"
#include "timers.h"

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

/**
 * @brief Programa principal
 */
int main() {
    stdio_init_all();

#ifdef CYW43_WL_GPIO_LED_PIN
    // Inicialización de chip de WiFi
    cyw43_arch_init();
#else
    // Inicialización de GPIO
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
#endif

    // Creación de Timers

    TimerHandle_t stimer_blinky = xTimerCreate(
        "Blinky Timer",     // Nombre del Timer
        pdMS_TO_TICKS(500), // Se dispara 500 ms después
        true,               // Auto reload
        0,                  // ID es 0
        soft_timer_blinky   // Callback del Timer
    );

    TimerHandle_t stimer_print = xTimerCreate(
        "Print Timer",      // Nombre del Timer,
        pdMS_TO_TICKS(2000),// Se dispara despues de 2 s
        false,              // One shot
        0,                  // ID es 0
        soft_timer_print    // Callback del Timer
    );

    // Inicia los Timers sin demora
    xTimerStart(stimer_blinky, 0);
    xTimerStart(stimer_print, 0);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(true);
}
