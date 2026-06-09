// --- Includes ---
#include "uart.h"
#include "driver/uart.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// --- Defines privados ---
#define UART_PORT_NUM UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_TX_PIN GPIO_NUM_43
#define UART_RX_PIN GPIO_NUM_44
#define UART_RX_BUF_SIZE 256

// --- Variables privadas ---
static const char *TAG = "uart";

// --- Prototipos privados ---
static void uart_init(void);
static void procesar_comando(const char *cmd);

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

        if (byte_leido <= 0)
            continue;       // no se recibió ningún byte, seguir esperando

        if (rx_byte == '\n')
        {
            if (cmd_len == 0)
                continue;   // línea vacía, ignorar

            cmd_buf[cmd_len] = '\0';
            procesar_comando(cmd_buf);
            cmd_len = 0;
        }
        else if (cmd_len < UART_RX_BUF_SIZE - 1)
        {
            cmd_buf[cmd_len++] = (char)rx_byte;
        }
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
    ESP_LOGI(TAG, "CMD: %s", cmd);
}
