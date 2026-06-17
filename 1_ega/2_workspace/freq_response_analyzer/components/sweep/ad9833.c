// --- Includes ---
#include "ad9833.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

// --- Defines privados ---
#define AD9833_PIN_CLK        GPIO_NUM_12       // SCLK SPI2
#define AD9833_PIN_DATA       GPIO_NUM_11       // SDATA SPI2
#define AD9833_PIN_FNC        GPIO_NUM_10       // Chip Select SPI2
#define AD9833_SPI_CLK_HZ     (1 * 1000 * 1000) // Frecuencia del bus SPI2
#define AD9833_SPI_MODE       2                 // SCLK idle alto CPOL=1, dato muestreado en el flanco de bajada CPHA=0

#define AD9833_MCLK_HZ        25000000UL        //Frecuencia del clock del modulo AD9833, 25 MHz

// --- Bits de dirección (D15:D14) ---
#define AD9833_REG_CTRL      (0 << 14)   // 00: registro de control
#define AD9833_REG_FREQ0     (1 << 14)   // 01: registro FREQ0
#define AD9833_REG_FREQ1     (2 << 14)   // 10: registro FREQ1
#define AD9833_REG_PHASE0    (3 << 14)   // 11: registro PHASE0/PHASE1

// --- Bits de control (solo para AD9833_REG_CTRL) ---
#define AD9833_CTRL_B28       (1 << 13)   // 1 = escribir FREQ en dos transferencias de 14 bits (LSB primero, luego MSB)
#define AD9833_CTRL_RESET     (1 << 8)    // 1 = DAC en reposo (midscale), 0 = salida activa

// --- Variables privadas ---
static const char *TAG = "ad9833";

static spi_device_handle_t spi_handle = NULL;
static uint16_t control_word = AD9833_REG_CTRL | AD9833_CTRL_B28 | AD9833_CTRL_RESET;

// --- Prototipos privados ---
static void ad9833_write_word(uint16_t word);

// --- Funciones ---

void ad9833_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = AD9833_PIN_DATA,
        .miso_io_num = -1,
        .sclk_io_num = AD9833_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = AD9833_SPI_CLK_HZ,
        .mode = AD9833_SPI_MODE,
        .spics_io_num = AD9833_PIN_FNC,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle));

    control_word = AD9833_REG_CTRL | AD9833_CTRL_B28 | AD9833_CTRL_RESET;
    ad9833_write_word(control_word);
    ad9833_set_freq(0); //iniciar con frecuencia en 0Hz

    ESP_LOGI(TAG, "AD9833 inicializado");
}

void ad9833_set_freq(uint32_t freq_hz)
{
    uint32_t freq_word = (uint32_t)(((uint64_t)freq_hz << 28) / AD9833_MCLK_HZ);    //FREQREG = (freq_hz × (2^28)) / MCLK_HZ
    
    // Escribir registro AD9833_REG_FREQ0 en dos transferencias de 14 bits
    ad9833_write_word(AD9833_REG_FREQ0 | (freq_word & 0x3FFF));         // 14 LSBs
    ad9833_write_word(AD9833_REG_FREQ0 | ((freq_word >> 14) & 0x3FFF)); // 14 MSBs
}

void ad9833_enable_output(void)
{
    control_word &= ~AD9833_CTRL_RESET; // RESET=0: habilita la salida del DAC
    ad9833_write_word(control_word);
}

void ad9833_disable_output(void)
{
    control_word |= AD9833_CTRL_RESET;  // RESET=1: DAC en reposo (midscale)
    ad9833_write_word(control_word);
}

static void ad9833_write_word(uint16_t word)
{
    spi_transaction_t trans = {
        .length = 16,
        .flags = SPI_TRANS_USE_TXDATA,
        .tx_data = { (uint8_t)(word >> 8), (uint8_t)(word & 0xFF), 0, 0 },
    };
    ESP_ERROR_CHECK(spi_device_transmit(spi_handle, &trans));
}