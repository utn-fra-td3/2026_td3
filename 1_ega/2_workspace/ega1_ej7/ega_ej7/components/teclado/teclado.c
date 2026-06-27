#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "teclado.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define SCAN_PERIOD_MS 20U

/*
 * Cambiá estos pines según tu conexión real.
 * Filas: salidas
 * Columnas: entradas con pull-up
 */
static const gpio_num_t rowPins[4] = {
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_15,
    GPIO_NUM_16
};

static const gpio_num_t colPins[4] = {
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_8,
    GPIO_NUM_3
};

static const char keymap[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

static void keypad_gpio_init(void)
{
    /*
     * Filas como salidas.
     * Se dejan en alto.
     */
    for (int r = 0; r < 4; r++)
    {
        gpio_config_t row_cfg = {
            .pin_bit_mask = 1ULL << rowPins[r],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };

        gpio_config(&row_cfg);
        gpio_set_level(rowPins[r], 1);
    }

    /*
     * Columnas como entradas con pull-up.
     * Cuando se presiona una tecla, la columna lee 0.
     */
    for (int c = 0; c < 4; c++)
    {
        gpio_config_t col_cfg = {
            .pin_bit_mask = 1ULL << colPins[c],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };

        gpio_config(&col_cfg);
    }
}

static void rows_all_high(void)
{
    for (int r = 0; r < 4; r++)
    {
        gpio_set_level(rowPins[r], 1);
    }
}

static void row_active_low(int r)
{
    rows_all_high();
    gpio_set_level(rowPins[r], 0);
}

static char keypad_scan_once(void)
{
    for (int r = 0; r < 4; r++)
    {
        row_active_low(r);

        /*
         * Pequeña espera para estabilizar la lectura.
         */
        vTaskDelay(pdMS_TO_TICKS(10));

        for (int c = 0; c < 4; c++)
        {
            if (gpio_get_level(colPins[c]) == 0)
            {
                rows_all_high();
                return keymap[r][c];
            }
        }
    }

    rows_all_high();
    return 0;
}

static char keypad_get_key_oneshot(void)
{
    char k = keypad_scan_once();

    if (k == 0)
    {
        return 0;
    }

    /*
     * Espera a que se suelte la tecla.
     * Esto evita repetir la misma tecla muchas veces.
     */
    while (keypad_scan_once() != 0)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return k;
}

static void enviar_tecla_a_intro(uint8_t numero)
{
    intro_t dato_intro = numero;

    xQueueSend(intro, &dato_intro, portMAX_DELAY);
}

void teclado_init(void)
{
    xTaskCreate(task_teclado, "task_teclado", 2048, NULL, 2, NULL);
}

void task_teclado(void *param)
{
    (void)param;

    keypad_gpio_init();

    TickType_t lastWake = xTaskGetTickCount(); // Inicializa el tiempo de referencia para el retardo periódico

    while (1)
    {
        char k = keypad_get_key_oneshot();

        if (k >= '1' && k <= '7')
        {
            uint8_t numero = (uint8_t)(k - '0');
            intro_t dato_intro = numero;
            xQueueSend(intro, &dato_intro, portMAX_DELAY);
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}