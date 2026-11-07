#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void uart_console_init(void);
void uart_console_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif
