#include "uart_protocol.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static char *next_token(char **cursor)
{
    char *start = *cursor;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }

    char *end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t') {
        end++;
    }
    if (*end != '\0') {
        *end++ = '\0';
    }
    *cursor = end;
    return start;
}

int uart_protocol_parse(char *line, uart_request_t *request)
{
    char *cursor = line;
    char *verb = next_token(&cursor);
    char *field = next_token(&cursor);
    char *value = next_token(&cursor);
    if (verb == NULL || field == NULL || next_token(&cursor) != NULL) {
        return 1;
    }

    request->value = 0.0f;
    if (strcmp(verb, "SET") == 0) {
        request->is_set = true;
        if (value == NULL) {
            return 1;
        }
    } else if (strcmp(verb, "GET") == 0 && value == NULL) {
        request->is_set = false;
    } else {
        return 1;
    }

    if (strcmp(field, "KP") == 0) {
        request->field = UART_FIELD_KP;
    } else if (strcmp(field, "KI") == 0) {
        request->field = UART_FIELD_KI;
    } else if (strcmp(field, "KD") == 0) {
        request->field = UART_FIELD_KD;
    } else if (strcmp(field, "PROFILE") == 0) {
        request->field = UART_FIELD_PROFILE;
    } else if (request->is_set && strcmp(field, "ANGLE") == 0) {
        request->field = UART_FIELD_ANGLE;
    } else if (!request->is_set && strcmp(field, "POSITION") == 0) {
        request->field = UART_FIELD_POSITION;
    } else if (!request->is_set && strcmp(field, "SETPOINT") == 0) {
        request->field = UART_FIELD_SETPOINT;
    } else if (!request->is_set && strcmp(field, "STATUS") == 0) {
        request->field = UART_FIELD_STATUS;
    } else {
        return 1;
    }

    if (!request->is_set) {
        return 0;
    }
    if (request->field == UART_FIELD_PROFILE) {
        if (strcmp(value, "ESCALON") == 0) {
            request->value = 0.0f;
        } else if (strcmp(value, "RAMPA") == 0) {
            request->value = 1.0f;
        } else {
            // Un nombre valido con texto agregado es un comando mal formado.
            return (strncmp(value, "ESCALON", 7) == 0 ||
                    strncmp(value, "RAMPA", 5) == 0) ? 1 : 2;
        }
        return 0;
    }

    char *end;
    float number = strtof(value, &end);
    if (end == value) {
        return 2;
    }
    if (*end != '\0') {
        return 1;
    }
    if (!isfinite(number) || number < 0.0f ||
        (request->field == UART_FIELD_ANGLE && number > 360.0f)) {
        return 2;
    }
    request->value = number;
    return 0;
}

int uart_protocol_feed(uart_line_reader_t *reader, unsigned char ch, uart_request_t *request)
{
    if (reader->skip_lf && ch == '\n') {
        reader->skip_lf = false;
        return -1;
    }
    reader->skip_lf = false;

    if (ch == '\r' || ch == '\n') {
        reader->data[reader->length] = '\0';
        int result = reader->rejected ? 1 : uart_protocol_parse(reader->data, request);
        reader->length = 0;
        reader->rejected = false;
        reader->skip_lf = ch == '\r';
        return result;
    }

    if (reader->rejected) {
        return -1;
    }
    if ((ch < 32 && ch != '\t') || ch > 126 ||
        reader->length >= sizeof(reader->data) - 1) {
        reader->rejected = true;
        return -1;
    }
    reader->data[reader->length++] = (char)ch;
    return -1;
}
