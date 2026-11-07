#include "ad9834_driver.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AD9834_CMD_RESET     0x0100U
#define AD9834_CMD_B28       0x2000U
#define AD9834_CMD_FSELECT1  0x0800U
#define AD9834_CMD_SLEEP1    0x0080U
#define AD9834_CMD_SLEEP12   0x0040U
#define AD9834_CMD_OPBITEN   0x0020U
#define AD9834_CMD_SIGNPIB   0x0010U
#define AD9834_CMD_DIV2      0x0008U
#define AD9834_CMD_MODE      0x0002U

static const char *TAG = "AD9834";
static spi_device_handle_t s_spi_handle = NULL;
static ad9834_cfg_t s_cfg = {0};
static uint16_t s_control = AD9834_CMD_B28 | AD9834_CMD_RESET;

static void ad9834_set_cs(bool level)
{
    gpio_set_level(s_cfg.cs_gpio, level ? 1 : 0);
}

static void ad9834_set_reset(bool level)
{
    gpio_set_level(s_cfg.reset_gpio, level ? 1 : 0);
}

void ad9834_write_register(uint16_t value)
{
    if (s_spi_handle == NULL) {
        ESP_LOGE(TAG, "SPI no inicializado");
        return;
    }

    ad9834_set_cs(false);

    uint8_t tx_data[2] = {(uint8_t)(value >> 8U), (uint8_t)value};
    spi_transaction_t trans = {
        .length = 16,
        .tx_buffer = tx_data,
        .rx_buffer = NULL,
    };

    esp_err_t ret = spi_device_transmit(s_spi_handle, &trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error enviando registro AD9834: %s", esp_err_to_name(ret));
        ad9834_set_cs(true);
        return;
    }

    ad9834_set_cs(true);
    ESP_LOGD(TAG, "SPI TX: 0x%04x", (unsigned int)value);
}

esp_err_t ad9834_init(const ad9834_cfg_t *cfg)
{
    if (cfg == NULL || (cfg->freq_register != AD9834_FREQ_REG0 &&
                        cfg->freq_register != AD9834_FREQ_REG1)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cfg = *cfg;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_cfg.cs_gpio) | (1ULL << s_cfg.reset_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar GPIOs del AD9834: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(s_cfg.cs_gpio, 1);
    gpio_set_level(s_cfg.reset_gpio, 0);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = s_cfg.mosi_gpio,
        .miso_io_num = -1,
        .sclk_io_num = s_cfg.sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };
    ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar el bus SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 2,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = 0,
    };

    ret = spi_bus_add_device(SPI3_HOST, &dev_cfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo agregar el dispositivo AD9834 al bus SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    ad9834_reset();
    ad9834_write_register(0xC000U);
    ad9834_write_register(0xE000U);
    ad9834_set_frequency_hz(0U, AD9834_FREQ_REG0);
    ad9834_set_frequency_hz(0U, AD9834_FREQ_REG1);

    ESP_LOGI(TAG, "AD9834 configurado por SPI, MCLK=%lu Hz; salida en reset",
             (unsigned long)s_cfg.mclk_hz);
    return ESP_OK;
}

uint32_t ad9834_get_max_frequency_hz(void)
{
    uint32_t mclk_hz = s_cfg.mclk_hz != 0U ? s_cfg.mclk_hz : AD9834_MCLK_DEFAULT;
    return mclk_hz / 2U;
}

void ad9834_reset(void)
{
    ad9834_set_reset(true);
    vTaskDelay(pdMS_TO_TICKS(1));
    s_control = AD9834_CMD_B28 | AD9834_CMD_RESET;
    if (s_cfg.freq_register == AD9834_FREQ_REG1) {
        s_control |= AD9834_CMD_FSELECT1;
    }
    ad9834_write_register(s_control);
    ad9834_set_reset(false);
}

void ad9834_enable(bool enabled)
{
    if (enabled) {
        s_control &= ~(AD9834_CMD_RESET | AD9834_CMD_SLEEP12 | AD9834_CMD_SLEEP1);
    } else {
        s_control |= AD9834_CMD_RESET | AD9834_CMD_SLEEP12 | AD9834_CMD_SLEEP1;
    }
    ad9834_write_register(s_control);
}

void ad9834_set_output_sine(void)
{
    s_control &= ~(AD9834_CMD_MODE | AD9834_CMD_OPBITEN |
                   AD9834_CMD_SIGNPIB | AD9834_CMD_DIV2);
    ad9834_write_register(s_control);
}

void ad9834_set_output_triangle(void)
{
    s_control &= ~(AD9834_CMD_OPBITEN | AD9834_CMD_SIGNPIB | AD9834_CMD_DIV2);
    s_control |= AD9834_CMD_MODE;
    ad9834_write_register(s_control);
}

void ad9834_set_output_square(void)
{
    s_control &= ~(AD9834_CMD_MODE | AD9834_CMD_SIGNPIB);
    s_control |= AD9834_CMD_OPBITEN | AD9834_CMD_DIV2;
    ad9834_write_register(s_control);
}

void ad9834_set_frequency_hz(uint32_t frequency_hz, ad9834_frequency_register_t register_select)
{
    if (register_select != AD9834_FREQ_REG0 && register_select != AD9834_FREQ_REG1) {
        ESP_LOGE(TAG, "Registro de frecuencia invalido");
        return;
    }

    if (s_cfg.mclk_hz == 0U) {
        s_cfg.mclk_hz = AD9834_MCLK_DEFAULT;
    }

    if (frequency_hz > ad9834_get_max_frequency_hz()) {
        ESP_LOGE(TAG, "Frecuencia fuera de rango");
        return;
    }

    uint64_t freq_word = ((uint64_t)frequency_hz << 28U) / s_cfg.mclk_hz;
    uint16_t lower_bits = (uint16_t)(freq_word & 0x3FFFU);
    uint16_t upper_bits = (uint16_t)((freq_word >> 14U) & 0x3FFFU);

    uint16_t freq_reg = register_select == AD9834_FREQ_REG1 ? 0x8000U : 0x4000U;
    s_control |= AD9834_CMD_B28;
    ad9834_write_register(s_control);
    ad9834_write_register((uint16_t)(freq_reg | lower_bits));
    ad9834_write_register((uint16_t)(freq_reg | upper_bits));
}
