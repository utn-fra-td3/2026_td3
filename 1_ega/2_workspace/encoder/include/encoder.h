#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENCODER_BUTTON_EVENT_NONE = 0,
    ENCODER_BUTTON_EVENT_SHORT,
    ENCODER_BUTTON_EVENT_LONG
} encoder_button_event_t;

typedef struct {
    uint32_t clk_gpio;
    uint32_t dt_gpio;
    uint32_t switch_gpio;
    uint8_t previous_state;
    int8_t movement;
    bool button_pressed;
    uint32_t button_press_ms;
    uint32_t button_change_ms;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
} encoder_t;

bool encoder_init(encoder_t *encoder, uint32_t clk_gpio, uint32_t dt_gpio, uint32_t switch_gpio, uint32_t debounce_ms, uint32_t long_press_ms, uint32_t now_ms);
int encoder_poll_rotation(encoder_t *encoder);
encoder_button_event_t encoder_poll_button(encoder_t *encoder, uint32_t now_ms);

#endif
