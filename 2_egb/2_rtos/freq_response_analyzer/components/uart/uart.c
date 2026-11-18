// --- Includes ---
#include "uart.h"
#include "driver/uart.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Defines privados ---
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_TX_PIN GPIO_NUM_43
#define UART_RX_PIN GPIO_NUM_44
#define UART_RX_BUF_SIZE 256
#define UART_TX_BUF_SIZE 128

// --- Tipos privados ---
typedef struct
{
    const char *nombre;
    sweep_param_e param;
} comando_param_t;

typedef struct
{
    const char *nombre;
    menu_evt_e evento;
} comando_t;

// --- Variables privadas ---
static const char *TAG = "uart";

// Parametros de get / set
static const comando_param_t TABLA_PARAMS[] = {
    {"frec_inicio", SWEEP_PARAM_FREC_INICIO},
    {"frec_final", SWEEP_PARAM_FREC_FINAL},
    {"puntos", SWEEP_PARAM_PUNTOS},
    {"tiempo", SWEEP_PARAM_TIEMPO},
};
#define CANT_PARAMS (sizeof(TABLA_PARAMS) / sizeof(TABLA_PARAMS[0]))

// Comandos reconocidos
static const comando_t TABLA_CMDS[] = {
    {"set", MENU_EVT_CONFIG_SET},
    {"get", MENU_EVT_CONFIG_GET},
    {"start", MENU_EVT_START},
    {"pause", MENU_EVT_PAUSE},
    {"resume", MENU_EVT_RESUME},
    {"cancel", MENU_EVT_CANCEL},
};
#define CANT_CMDS (sizeof(TABLA_CMDS) / sizeof(TABLA_CMDS[0]))

// --- Prototipos privados ---
static void uart_init(void);
static void procesar_comando(char *linea);
static bool buscar_param(const char *nombre, sweep_param_e *param);
static bool parsear_uint(const char *s, uint32_t *out);
static void responder_estado(uart_status_e status);
static void procesar_queue_uart_tx(void);
static void enviar_msg(const uart_tx_msg_t *tx);

// --- Funciones ---

void task_uart(void *pvParameters)
{
    uart_init();

    char cmd_buf[UART_RX_BUF_SIZE];
    int cmd_len = 0;
    bool linea_larga = false;
    uint8_t rx_byte;

    while (1)
    {
        int byte_leido = uart_read_bytes(UART_PORT_NUM, &rx_byte, 1, pdMS_TO_TICKS(100));

        if (byte_leido > 0)
        {
            if (rx_byte == '\r' || rx_byte == '\n')
            {
                if (linea_larga) // la linea no entro en el buffer: se descarta entera
                {
                    ESP_LOGW(TAG, "linea demasiado larga");
                    responder_estado(UART_STATUS_SYNTAX);
                    linea_larga = false;
                    cmd_len = 0;
                }
                else if (cmd_len > 0) // si la linea no esta vacia se procesa
                {
                    cmd_buf[cmd_len] = '\0';
                    ESP_LOGI(TAG, "comando recibido: \"%s\"", cmd_buf);
                    procesar_comando(cmd_buf);
                    cmd_len = 0;
                }
            }
            else if (cmd_len < sizeof(cmd_buf) - 1)
            {
                cmd_buf[cmd_len++] = (char)rx_byte;
            }
            else
            {
                linea_larga = true; // se ignora el resto hasta el fin de linea
            }
        }

        procesar_queue_uart_tx();
    }
}

static void uart_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART inicializado @ %d baud", UART_BAUD_RATE);
}

static void procesar_comando(char *linea)
{
    char *cmd = strtok(linea, " ");
    char *parametro = strtok(NULL, " ");
    char *valor = strtok(NULL, " ");
    char *sobra = strtok(NULL, " ");
    menu_event_msg_t ev = {.origin = EVENT_ORIGIN_UART};
    uart_status_e estado = UART_STATUS_UNKNOWN_CMD;

    if (cmd == NULL)
    {
        ESP_LOGW(TAG, "linea sin comando");
        responder_estado(estado);
        return;
    }

    // Verificamos tipo de comando
    for (uint8_t i = 0; i < CANT_CMDS; i++)
    {
        if (strcmp(cmd, TABLA_CMDS[i].nombre) == 0)
        {
            ev.type = TABLA_CMDS[i].evento;
            estado = UART_STATUS_OK;
            break;
        }
    }

    // Verificamos parametro y valor segun el tipo de comando
    if (estado == UART_STATUS_OK)
    {
        switch (ev.type)
        {
        case MENU_EVT_CONFIG_SET:
            if (parametro == NULL || valor == NULL || sobra != NULL)
                estado = UART_STATUS_SYNTAX;
            else if (!buscar_param(parametro, &ev.set.param))
                estado = UART_STATUS_SYNTAX;
            else if (!parsear_uint(valor, &ev.set.value))
                estado = UART_STATUS_SYNTAX;
            break;

        case MENU_EVT_CONFIG_GET:
            if (parametro == NULL || valor != NULL)
                estado = UART_STATUS_SYNTAX;
            else if (strcmp(parametro, "config") == 0)
                ev.get_param = SWEEP_PARAM_ALL;
            else if (!buscar_param(parametro, &ev.get_param))
                estado = UART_STATUS_SYNTAX;
            break;

        default: // start, pause, resume, cancel (sin argumentos)
            if (parametro != NULL)
                estado = UART_STATUS_SYNTAX;
            break;
        }
    }

    // Envio del evento al menu o responder con error por UART
    if (estado != UART_STATUS_OK)
    {
        ESP_LOGW(TAG, "comando invalido (ERR %d): %s", estado, cmd);
        responder_estado(estado);
    }
    else
    {
        xQueueSend(queue_menu_events, &ev, portMAX_DELAY);
    }
}

static bool buscar_param(const char *nombre, sweep_param_e *param)
{
    for (uint8_t i = 0; i < CANT_PARAMS; i++)
    {
        if (strcmp(nombre, TABLA_PARAMS[i].nombre) == 0)
        {
            *param = TABLA_PARAMS[i].param;
            return true;
        }
    }
    return false;
}

static bool parsear_uint(const char *s, uint32_t *out)
{
    // Verifico si la cadena son solo digitos
    for (uint8_t i = 0; s[i] != '\0'; i++)
    {
        if (!isdigit((unsigned char)s[i]))
        {
            return false;
        }
    }

    *out = strtoul(s, NULL, 10);
    return true;
}

// Respuesta directa de task_uart para los errores detectados
static void responder_estado(uart_status_e status)
{
    uart_tx_msg_t tx = {
        .type = UART_TX_STATUS,
        .status = status,
    };
    enviar_msg(&tx);
}

static void procesar_queue_uart_tx(void)
{
    uart_tx_msg_t tx;

    while (xQueueReceive(queue_uart_tx, &tx, 0) == pdTRUE)
    {
        enviar_msg(&tx);
    }
}

// Formatea un mensaje segun su tipo y lo escribe por UART
static void enviar_msg(const uart_tx_msg_t *tx)
{
    char buf[UART_TX_BUF_SIZE];
    int len;

    switch (tx->type)
    {
    case UART_TX_POINT:
        len = snprintf(buf, sizeof(buf), "POINT i=%lu freq=%lu db=%.2f\n", tx->point.index, tx->point.freq_hz, tx->point.db);
        break;

    case UART_TX_STATUS:
        if (tx->status == UART_STATUS_OK)
        {
            len = snprintf(buf, sizeof(buf), "OK\n");
        }
        else
        {
            len = snprintf(buf, sizeof(buf), "ERR %d\n", tx->status);
        }
        break;

    case UART_TX_CONFIG:
        switch (tx->get.param)
        {
        case SWEEP_PARAM_FREC_INICIO:
            len = snprintf(buf, sizeof(buf), "frec_inicio=%lu\n", tx->get.config.frec_inicio);
            break;
        case SWEEP_PARAM_FREC_FINAL:
            len = snprintf(buf, sizeof(buf), "frec_final=%lu\n", tx->get.config.frec_final);
            break;
        case SWEEP_PARAM_PUNTOS:
            len = snprintf(buf, sizeof(buf), "puntos=%lu\n", tx->get.config.puntos);
            break;
        case SWEEP_PARAM_TIEMPO:
            len = snprintf(buf, sizeof(buf), "tiempo=%lu\n", tx->get.config.tiempo);
            break;
        default: // SWEEP_PARAM_ALL
            len = snprintf(buf, sizeof(buf), "frec_inicio=%lu frec_final=%lu puntos=%lu tiempo=%lu\n",
                           tx->get.config.frec_inicio, tx->get.config.frec_final,
                           tx->get.config.puntos, tx->get.config.tiempo);
            break;
        }
        break;

    case UART_TX_SWEEP_START:
        len = snprintf(buf, sizeof(buf), "SWEEP frec_inicio=%lu frec_final=%lu puntos=%lu tiempo=%lu\n",
                       tx->config.frec_inicio, tx->config.frec_final,
                       tx->config.puntos, tx->config.tiempo);
        break;

    default:
        ESP_LOGW(TAG, "tipo de mensaje uart_tx desconocido: %d", tx->type);
        return;
    }

    uart_write_bytes(UART_PORT_NUM, buf, len);
}
