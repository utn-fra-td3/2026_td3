// --- Includes ---
#include "uart.h"
#include "driver/uart.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// --- Defines privados ---
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_TX_PIN GPIO_NUM_43
#define UART_RX_PIN GPIO_NUM_44
#define UART_RX_BUF_SIZE 256

// --- Tipos privados ---
typedef struct {
    const char *  nombre;
    sweep_param_e param;
} comando_param_t;

// --- Variables privadas ---
static const char *TAG = "uart";

static const comando_param_t TABLA_PARAMS[] = {
    {"frec_inicio", SWEEP_PARAM_FREC_INICIO},
    {"frec_final",  SWEEP_PARAM_FREC_FINAL},
    {"puntos",      SWEEP_PARAM_PUNTOS},
    {"tiempo",      SWEEP_PARAM_TIEMPO},
};
#define CANT_PARAMS (sizeof(TABLA_PARAMS) / sizeof(TABLA_PARAMS[0]))

// --- Prototipos privados ---
static void uart_init(void);
static void procesar_comando(const char *cmd);
static void procesar_set(const char *nombre_param, uint32_t value);
static void enviar_evento_menu(menu_evt_e evento);
static void enviar_uart(const char *msg, size_t len);
static void procesar_queue_uart_tx(void);
static void formatear_uart_tx(const uart_tx_msg_t *tx, char *buf, size_t buf_len, int *len);

// --- Funciones ---

void task_uart(void *pvParameters)
{
    uart_init();

    char cmd_buf[UART_RX_BUF_SIZE];
    int cmd_len = 0;
    uint8_t rx_byte;

    while (1)
    {
        int byte_leido = uart_read_bytes(UART_PORT_NUM, &rx_byte, 1, pdMS_TO_TICKS(100));

        if (byte_leido > 0)
        {
            if (rx_byte == '\r' || rx_byte == '\n')
            {
                if (cmd_len > 0) // linea vacia, ignorar
                {
                    cmd_buf[cmd_len] = '\0';
                    ESP_LOGD(TAG, "comando recibido: \"%s\"", cmd_buf);
                    procesar_comando(cmd_buf);
                    cmd_len = 0;
                }
            }
            else if (cmd_len < UART_RX_BUF_SIZE - 1)
            {
                cmd_buf[cmd_len++] = (char)rx_byte;
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

static void procesar_comando(const char *cmd)
{
    char     nombre_param[16];
    uint32_t value;

    if (sscanf(cmd, "set %15s %lu", nombre_param, &value) == 2)
    {
        procesar_set(nombre_param, value);
        return;
    }

    if (strcmp(cmd, "start") == 0)
    {
        enviar_evento_menu(MENU_EVT_BTN_START);
        return;
    }

    if (strcmp(cmd, "pause") == 0)
    {
        enviar_evento_menu(MENU_EVT_BTN_PAUSE);
        return;
    }

    if (strcmp(cmd, "cancel") == 0)
    {
        enviar_evento_menu(MENU_EVT_BTN_CANCEL);
        return;
    }

    ESP_LOGW(TAG, "comando no reconocido: %s", cmd);
}

static void procesar_set(const char *nombre_param, uint32_t value)
{
    for (size_t i = 0; i < CANT_PARAMS; i++)
    {
        if (strcmp(nombre_param, TABLA_PARAMS[i].nombre) != 0)
        {
            continue;
        }

        menu_event_msg_t ev = {
            .type  = MENU_EVT_CONFIG_SET,
            .param = TABLA_PARAMS[i].param,
            .value = value,
        };
        xQueueSend(queue_menu_events, &ev, portMAX_DELAY);
        return;
    }

    ESP_LOGW(TAG, "parametro desconocido: %s", nombre_param);
}

static void enviar_evento_menu(menu_evt_e evento)
{
    menu_event_msg_t ev = {.type = evento};
    xQueueSend(queue_menu_events, &ev, portMAX_DELAY);
}

static void enviar_uart(const char *msg, size_t len)
{
    uart_write_bytes(UART_PORT_NUM, msg, len);
}

static void procesar_queue_uart_tx(void)
{
    uart_tx_msg_t tx;
    char          buf[64];
    int           len;

    while (xQueueReceive(queue_uart_tx, &tx, 0) == pdTRUE)
    {
        formatear_uart_tx(&tx, buf, sizeof(buf), &len);
        enviar_uart(buf, len);
    }
}

static void formatear_uart_tx(const uart_tx_msg_t *tx, char *buf, size_t buf_len, int *len)
{
    *len = snprintf(buf, buf_len, "POINT freq=%lu db=%.2f\n", tx->freq_hz, tx->db);
}
