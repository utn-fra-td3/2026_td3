// --- Includes ---
#include "menu_config.h"
#include "esp_log.h"

// --- Tipos privados ---
typedef struct {
    uint32_t frec_inicio;
    uint32_t frec_final;
    uint32_t puntos;
    uint32_t tiempo;
} sweep_config_t;

// --- Variables privadas ---
static const char *TAG = "menu_config";

static sweep_config_t config = {
    .frec_inicio = 10,
    .frec_final  = 100000,
    .puntos      = 200,
    .tiempo      = 5,
};

static const uint32_t MIN[] = {10,     10,     2,   1};
static const uint32_t MAX[] = {100000, 100000, 200, 60};

static uint32_t *campo[] = {
    &config.frec_inicio, &config.frec_final,
    &config.puntos,      &config.tiempo
};

// --- Prototipos privados ---
static void procesar_config_set(sweep_param_e param, uint32_t value);

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
        ESP_LOGW(TAG, "valor fuera de rango: param=%d value=%lu, se mantiene el valor anterior", param, value);
    else
        *campo[param] = value;

    // Siempre se reenvia el valor vigente (nuevo si fue valido, el anterior si no),
    // asi lcd_display nunca queda mostrando un valor sin confirmar por este componente.
    display_msg_t msg = {
        .type  = DISPLAY_MSG_CONFIG_VALUE,
        .param = param,
        .value = *campo[param],
    };
    if (xQueueSend(queue_display, &msg, 0) != pdTRUE)
        ESP_LOGW(TAG, "queue_display llena, valor no mostrado");
}
