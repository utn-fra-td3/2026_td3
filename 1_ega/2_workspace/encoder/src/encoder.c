#include "encoder.h"

#include <stddef.h>

#include "hardware/gpio.h"

static void encoder_configure_gpio(uint32_t gpio) {
    gpio_init((uint)gpio);
    gpio_set_dir((uint)gpio, GPIO_IN);
    gpio_pull_up((uint)gpio);
}

bool encoder_init(encoder_t *encoder, uint32_t clk_gpio, uint32_t dt_gpio, uint32_t switch_gpio, uint32_t debounce_ms, uint32_t long_press_ms, uint32_t now_ms) {
    if (encoder == NULL || long_press_ms < debounce_ms) return false;
    encoder_configure_gpio(clk_gpio);
    encoder_configure_gpio(dt_gpio);
    encoder_configure_gpio(switch_gpio);
    encoder->clk_gpio = clk_gpio;
    encoder->dt_gpio = dt_gpio;
    encoder->switch_gpio = switch_gpio;
    encoder->previous_state = (uint8_t)((gpio_get((uint)clk_gpio) << 1U) | gpio_get((uint)dt_gpio));
    encoder->movement = 0;
    encoder->button_pressed = !gpio_get((uint)switch_gpio);
    encoder->button_press_ms = now_ms;
    encoder->button_change_ms = now_ms;
    encoder->debounce_ms = debounce_ms;
    encoder->long_press_ms = long_press_ms;
    return true;
}

int encoder_poll_rotation(encoder_t *encoder) {
    if (encoder == NULL) return 0;
    static const int8_t transition_table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t current_state = (uint8_t)((gpio_get((uint)encoder->clk_gpio) << 1U) | gpio_get((uint)encoder->dt_gpio));
    encoder->movement += transition_table[(encoder->previous_state << 2U) | current_state];
    encoder->previous_state = current_state;
    if (encoder->movement >= 4) {
        encoder->movement = 0;
        return 1;
    }
    if (encoder->movement <= -4) {
        encoder->movement = 0;
        return -1;
    }
    return 0;
}

encoder_button_event_t encoder_poll_button(encoder_t *encoder, uint32_t now_ms) {
    if (encoder == NULL) return ENCODER_BUTTON_EVENT_NONE;
    bool pressed = !gpio_get((uint)encoder->switch_gpio);
    if (pressed == encoder->button_pressed || (now_ms - encoder->button_change_ms) < encoder->debounce_ms) return ENCODER_BUTTON_EVENT_NONE;
    encoder->button_pressed = pressed;
    encoder->button_change_ms = now_ms;
    if (pressed) {
        encoder->button_press_ms = now_ms;
        return ENCODER_BUTTON_EVENT_NONE;
    }
    return (now_ms - encoder->button_press_ms) >= encoder->long_press_ms ? ENCODER_BUTTON_EVENT_LONG : ENCODER_BUTTON_EVENT_SHORT;
}
