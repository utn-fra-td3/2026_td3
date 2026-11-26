#include "ad9833.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "guardiana.h"

#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_CS   13

static spi_device_handle_t spi;
static SemaphoreHandle_t mutex_spi = NULL;

static void ad9833_write(uint16_t data) {
    spi_transaction_t t = {};
    t.length = 16;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = (data >> 8) & 0xFF;
    t.tx_data[1] = data & 0xFF;
    spi_device_polling_transmit(spi, &t);
}

void ad9833_init(void) {
    mutex_spi = xSemaphoreCreateMutex();

    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = SPI_MOSI,
        .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 2,
        .spics_io_num = SPI_CS,
        .queue_size = 1
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    imprimir_consola("Bus SPI y AD9833 inicializados.\n");
    ad9833_set_frequency(1000); // Frecuencia inicial de 1 kHz por defecto.
}

void ad9833_set_frequency(uint32_t freq_hz) {
    if (xSemaphoreTake(mutex_spi, portMAX_DELAY) == pdTRUE) {
        // Cálculo de los registros para el AD9833 (CLK de 25MHz)
        uint32_t freq_reg = (uint32_t)(((uint64_t)freq_hz << 28) / 25000000);
        uint16_t lsb = (freq_reg & 0x3FFF) | 0x4000;
        uint16_t msb = ((freq_reg >> 14) & 0x3FFF) | 0x4000;

        ad9833_write(0x2100); // Reset ON
        ad9833_write(lsb);    // FREQ0 LSB
        ad9833_write(msb);    // FREQ0 MSB
        ad9833_write(0xC000); // Phase 0
        ad9833_write(0x2000); // Reset OFF, Salida Senoidal
        
        xSemaphoreGive(mutex_spi);
    }
}