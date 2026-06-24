// --- Includes ---
#include "menu_config.h"
#include "esp_log.h"

// --- Variables privadas ---
static const char *TAG = "menu_config";

static sweep_config_t config = {
    .frec_inicio = 10,
    .frec_final = 100000,
    .puntos = 200,
    .tiempo = 25,
};

static const uint32_t MIN[] = {10, 11, 2, 1};
static const uint32_t MAX[] = {99999, 100000, 512, 100};

static uint32_t *campo[] = {
    &config.frec_inicio, &config.frec_final,
    &config.puntos, &config.tiempo};

// --- Prototipos privados ---
static void procesar_config_set(sweep_param_e param, uint32_t value);
static void procesar_sweep_start(void);
static sweep_start_result_e validar_config_completa(void);

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
            case MENU_EVT_SWEEP_START:
                procesar_sweep_start();
                break;
            }
        }
    }
}

static void procesar_config_set(sweep_param_e param, uint32_t value)
{
    if (value < MIN[param] || value > MAX[param])
    {
        ESP_LOGW(TAG, "valor fuera de rango: param=%d value=%lu, se mantiene el valor anterior", param, value);
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

// Orden de verificacion: rango de cada frecuencia, relacion entre ambas,
// rango de puntos y por ultimo el asentamiento (depende de frec_inicio).
static sweep_start_result_e validar_config_completa(void)
{
    if (config.frec_inicio < MIN[SWEEP_PARAM_FREC_INICIO] || config.frec_inicio > MAX[SWEEP_PARAM_FREC_INICIO])
        return SWEEP_START_ERR_FSTART_RANGE;

    if (config.frec_final < MIN[SWEEP_PARAM_FREC_FINAL] || config.frec_final > MAX[SWEEP_PARAM_FREC_FINAL])
        return SWEEP_START_ERR_FSTOP_RANGE;

    if (config.frec_inicio >= config.frec_final)
        return SWEEP_START_ERR_FSTART_GE_FSTOP;

    if (config.puntos < MIN[SWEEP_PARAM_PUNTOS] || config.puntos > MAX[SWEEP_PARAM_PUNTOS])
        return SWEEP_START_ERR_POINTS_RANGE;

    // settle_min = (1/f_start)/4 segundos = 1000/(4*f_start) ms = 250/f_start ms
    uint32_t settle_min_ms = (250 + config.frec_inicio - 1) / config.frec_inicio; // redondeo hacia arriba
    if (config.tiempo < settle_min_ms)
        return SWEEP_START_ERR_SETTLE_TOO_LOW;

    return SWEEP_START_OK;
}

static void procesar_sweep_start(void)
{
    sweep_start_result_e resultado = validar_config_completa();
    display_msg_t        msg = {0};

    if (resultado == SWEEP_START_OK)
    {
        ESP_LOGI(TAG, "configuracion valida, iniciando barrido");

        sweep_cmd_msg_t cmd = {
            .cmd    = SWEEP_CMD_START,
            .config = config,
        };
        if (xQueueSend(queue_sweep_cmd, &cmd, 0) != pdTRUE)
            ESP_LOGW(TAG, "queue_sweep_cmd llena, no se pudo iniciar el barrido");

        msg.type = DISPLAY_MSG_SWEEP_START_OK;
    }
    else
    {
        ESP_LOGW(TAG, "configuracion invalida para iniciar barrido: motivo=%d", resultado);
        msg.type   = DISPLAY_MSG_SWEEP_START_ERROR;
        msg.motivo = resultado;
    }

    if (xQueueSend(queue_display, &msg, 0) != pdTRUE)
        ESP_LOGW(TAG, "queue_display llena, resultado no mostrado");
}
