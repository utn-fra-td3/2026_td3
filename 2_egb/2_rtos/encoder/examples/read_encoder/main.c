#include <stdio.h>

#include "encoder.h"
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    encoder_t encoder;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (!encoder_init(&encoder, 2U, 3U, 4U, 30U, 800U, now_ms)) panic("Configuracion de encoder invalida");

    while (true) {
        int movement = encoder_poll_rotation(&encoder);
        encoder_button_event_t button = encoder_poll_button(&encoder, to_ms_since_boot(get_absolute_time()));
        if (movement != 0) printf("Giro: %d\n", movement);
        if (button == ENCODER_BUTTON_EVENT_SHORT) printf("Pulsacion corta\n");
        if (button == ENCODER_BUTTON_EVENT_LONG) printf("Pulsacion larga\n");
        sleep_ms(2U);
    }
}
