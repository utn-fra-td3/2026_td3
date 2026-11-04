#include <stdint.h>
#include "digipot.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

// Pines spi del MCP41000
#define PIN_CS      10
#define PIN_MOSI    11
#define PIN_SCK     12

// Valores del cursor del MCP41000 según la escala vertical
#define MCP_CURSOR_200MV_DIV  150
#define MCP_CURSOR_500MV_DIV  60

#define MCP_WRITE_CMD        0x11       // Comando de escritura del MCP41000

static spi_device_handle_t digipot_handle = NULL;       // Handle del dispositivo MCP41000

static void digipot_spi_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 8,
    };

    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };

    spi_bus_add_device(SPI2_HOST, &dev, &digipot_handle);
}

void digipot_set_cursor(uint8_t value)
{
    if (digipot_handle == NULL)
    {
        return;
    }

    uint8_t tx[2] = {MCP_WRITE_CMD, value};     // Buffer de transmisión

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
    };

    spi_device_transmit(digipot_handle, &t);
}

static uint8_t cursor_por_escala(uint8_t escala_vertical)
{
    if (escala_vertical == escala_200mv_div)
    {
        return MCP_CURSOR_200MV_DIV;
    }

    return MCP_CURSOR_500MV_DIV;
}

void digipot_init(void)
{
    digipot_spi_init();

    xTaskCreate(task_digipot, "task_digipot", 4096, NULL, 3, NULL);
}

void task_digipot(void *param)
{
    (void)param;        // Evita advertencis del compilador

    config_t config_actual;

    uint8_t escala_anterior = 0;
    uint8_t primera_vez = 1;
    uint8_t valor_cursor;

    while (config == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (1)
    {
        if (xQueuePeek(config, &config_actual, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            if (primera_vez || config_actual.escala_vertical != escala_anterior)
            {
                valor_cursor = cursor_por_escala(config_actual.escala_vertical);

                digipot_set_cursor(valor_cursor);

                escala_anterior = config_actual.escala_vertical;
                primera_vez = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}