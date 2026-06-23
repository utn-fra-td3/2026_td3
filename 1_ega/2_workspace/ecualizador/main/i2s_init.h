#pragma once
#include <stdio.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#define I2S_MCLK_GPIO   GPIO_NUM_14
#define I2S_BCLK_GPIO   GPIO_NUM_26
#define I2S_LRCK_GPIO   GPIO_NUM_25
#define I2S_DIN_GPIO    GPIO_NUM_34
#define I2S_DOUT_GPIO   GPIO_NUM_23

#define SAMPLE_RATE     48000
#define FRAMES_PER_BUF  128     // frames estéreo por buffer (= 256 int32_t)

extern i2s_chan_handle_t i2s_tx_handle;
extern i2s_chan_handle_t i2s_rx_handle;

// Toda la configuración en el header como static inline
static inline esp_err_t i2s_init_full_duplex(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);// El modo full-duplex requiere que ambos canales tengan la misma configuración
    chan_cfg.dma_desc_num  = 3;              // uno por slot del triple buffer
    chan_cfg.dma_frame_num = FRAMES_PER_BUF; // número de frames por buffer (tamaño del buffer)

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src        = I2S_CLK_SRC_APLL,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws   = I2S_LRCK_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din  = I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // Misma config para ambos canales — requisito del modo full-duplex
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg));

    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));

    return ESP_OK;
}