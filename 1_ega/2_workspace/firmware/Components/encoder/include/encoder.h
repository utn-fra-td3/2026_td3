#ifndef ENCODER_H
#define ENCODER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Tipos de eventos del encoder
typedef enum {
    ENCODER_UP,
    ENCODER_DOWN,
    ENCODER_CLICK,
    EVENTO_BARRIDO_FIN // Señal interna para despertar al menú
} encoder_event_t;

void encoder_init(QueueHandle_t cola_eventos);

#endif // ENCODER_H