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

bool encoder_process_event(encoder_t *encoder, uint32_t gpio, uint32_t levels, uint32_t now_ms, int *movement, encoder_button_event_t *button_event) {
    static const int8_t transition_table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

    if (encoder == NULL || movement == NULL || button_event == NULL) {
        return false;
    }

    *movement = 0;
    *button_event = ENCODER_BUTTON_EVENT_NONE;

    if (gpio == encoder->clk_gpio || gpio == encoder->dt_gpio) {
        uint8_t current_state = (uint8_t)((((levels >> encoder->clk_gpio) & 1U) << 1U) | ((levels >> encoder->dt_gpio) & 1U));
        encoder->movement += transition_table[(encoder->previous_state << 2U) | current_state];
        encoder->previous_state = current_state;
        if (encoder->movement >= 4) {
            encoder->movement = 0;
            *movement = 1;
        } else if (encoder->movement <= -4) {
            encoder->movement = 0;
            *movement = -1;
        }
        return true;
    }

    if (gpio == encoder->switch_gpio) {
        bool pressed = ((levels >> encoder->switch_gpio) & 1U) == 0U;
        if (pressed == encoder->button_pressed || (now_ms - encoder->button_change_ms) < encoder->debounce_ms) {
            return true;
        }
        encoder->button_pressed = pressed;
        encoder->button_change_ms = now_ms;
        if (pressed) {
            encoder->button_press_ms = now_ms;
        } else if ((now_ms - encoder->button_press_ms) >= encoder->long_press_ms) {
            *button_event = ENCODER_BUTTON_EVENT_LONG;
        } else {
            *button_event = ENCODER_BUTTON_EVENT_SHORT;
        }
        return true;
    }

    return false;
}
