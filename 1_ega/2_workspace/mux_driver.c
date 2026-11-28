#include "mux_driver.h"
#include "driver/gpio.h"

#define MUX_PIN_A  4
#define MUX_PIN_B  19

// Tabla de selección: {A, B} para cada canal
// C está fijo a GND en hardware
static const uint8_t mux_table[3][2] = {
    {0, 0},  // MUX_CH_SINE     → Y0
    {1, 0},  // MUX_CH_TRIANGLE → Y1
    {0, 1},  // MUX_CH_SQUARE   → Y2
};

void mux_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MUX_PIN_A) | (1ULL << MUX_PIN_B),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Estado inicial: canal 0 (senoidal)
    gpio_set_level(MUX_PIN_A, 0);
    gpio_set_level(MUX_PIN_B, 0);
}

void mux_select(mux_channel_t ch) {
    if (ch > MUX_CH_SQUARE) return;
    gpio_set_level(MUX_PIN_A, mux_table[ch][0]);
    gpio_set_level(MUX_PIN_B, mux_table[ch][1]);
}