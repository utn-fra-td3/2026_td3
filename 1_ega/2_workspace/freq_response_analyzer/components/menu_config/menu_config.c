// --- Includes ---
#include "menu_config.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <stdio.h>

// --- Defines privados ---
#define UART_PORT_NUM UART_NUM_0

// --- Tipos privados ---
typedef struct
{
    uint32_t frec_inicio;
    uint32_t frec_final;
    uint32_t puntos;
    uint32_t tiempo;
} sweep_config_t;

// --- Variables privadas ---
static const char *TAG = "menu_config";

static sweep_config_t config = {
    .frec_inicio = 10,
    .frec_final = 100000,
    .puntos = 200,
    .tiempo = 5,
};

static const uint32_t MIN[] = {10, 10, 2, 1};
static const uint32_t MAX[] = {100000, 100000, 200, 60};

static uint32_t *campo[] = {
    &config.frec_inicio, &config.frec_final,
    &config.puntos, &config.tiempo};

// --- Prototipos privados ---
static void procesar_config_set(sweep_param_e param, uint32_t value);
static void enviar_uart(const char *msg, size_t len);

// --- Funciones ---

void task_menu_config(void *pvParameters)
{
    menu_event_msg_t ev;

    while (1)
    {
        if (xQueueReceive(queue_menu_events, &ev, portMAX_DELAY) == pdTRUE)
        {
            switch (ev.type)
            {
            case MENU_EVT_CONFIG_SET:
                procesar_config_set(ev.param, ev.value);
                break;
            }
        }
    }
}

static void procesar_config_set(sweep_param_e param, uint32_t value)
{
    if (value < MIN[param] || value > MAX[param])
    {
        char buf[64];
        int  len = snprintf(buf, sizeof(buf), "ERROR valor fuera de rango: param=%d value=%lu\n", param, value);
        ESP_LOGW(TAG, "valor fuera de rango: param=%d value=%lu, se mantiene el valor anterior", param, value);
        enviar_uart(buf, len);
    }
    else
    {
        *campo[param] = value;
    }

    // Siempre se reenvia el valor vigente (nuevo si fue valido, el anterior si no),
    // asi lcd_display nunca queda mostrando un valor sin confirmar por este componente.
    display_msg_t msg = {
        .type = DISPLAY_MSG_CONFIG_VALUE,
        .param = param,
        .value = *campo[param],
    };
    if (xQueueSend(queue_display, &msg, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "queue_display llena, valor no mostrado");
    }
}

// TX por UART protegida con mutex_uart_tx: usada para avisar errores ahora,
// y reutilizable mas adelante para responder consultas (ej. "dame toda la config").
static void enviar_uart(const char *msg, size_t len)
{
    if (xSemaphoreTake(mutex_uart_tx, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        uart_write_bytes(UART_PORT_NUM, msg, len);
        xSemaphoreGive(mutex_uart_tx);
    }
    else
    {
        ESP_LOGW(TAG, "no se pudo tomar mutex_uart_tx, mensaje no enviado");
    }
}
