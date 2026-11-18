#pragma once

#include <stdbool.h>
#include <stddef.h>

#define UART_PROTOCOL_LINE_SIZE 256
#define UART_PROTOCOL_RESPONSE_SIZE 384

typedef enum {
    UART_FIELD_ANGLE,
    UART_FIELD_POSITION,
    UART_FIELD_SETPOINT,
    UART_FIELD_KP,
    UART_FIELD_KI,
    UART_FIELD_KD,
    UART_FIELD_PROFILE,
    UART_FIELD_STATUS
} uart_field_t;

typedef struct {
    bool is_set;
    uart_field_t field;
    float value;
} uart_request_t;

typedef struct {
    char data[UART_PROTOCOL_LINE_SIZE];
    size_t length;
    bool rejected;
    bool skip_lf;
} uart_line_reader_t;

// 0: request valido; 1: sintaxis invalida; 2: valor invalido.
int uart_protocol_parse(char *line, uart_request_t *request);
// -1: linea incompleta. Descarta una linea invalida entera antes de responder.
int uart_protocol_feed(uart_line_reader_t *reader, unsigned char ch, uart_request_t *request);
