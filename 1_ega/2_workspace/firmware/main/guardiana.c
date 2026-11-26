#include "guardiana.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "driver/uart.h"
#include "ad9833.h"

#define UART_PYTHON UART_NUM_0        // UART0 -> Salida/Entrada PC (USB)
#define UART_EGB    UART_NUM_1        // UART1 -> Comandos EGB (Raspberry Pi)

// Cola privada de la guardiana
static QueueHandle_t q_impresion = NULL;

typedef struct {
    char texto[256];
} msj_consola_t;

// Variables de estado local para responder las consultas del tipo GET de la EGB
static uint32_t f_ini_actual = 10;
static uint32_t f_fin_actual = 100000;
static uint32_t paso_actual = 1000;
static uint32_t tiempo_actual = 500;

void guardiana_init(void) {
    q_impresion = xQueueCreate(20, sizeof(msj_consola_t));

    // 1. Configurar UART0 para enviar y recibir datos desde PC (USB) permite enviar comandos al analizador por consola de vscode para pruebas rápidas de frecuencia
    uart_config_t uart_python_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_PYTHON, &uart_python_config);
    // Asignamos 512 bytes de buffer RX para recibir comandos desde la PC
    uart_driver_install(UART_PYTHON, 512, 0, 0, NULL, 0);

    // 2. Configurar UART1 para comandos de la EGB (Raspberry Pi)
    uart_config_t uart_egb_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_EGB, &uart_egb_config);
    uart_set_pin(UART_EGB, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_EGB, 256, 0, 0, NULL, 0);
}

// Función para enviar datos hacia la PC
void imprimir_consola(const char *formato, ...) {
    if (q_impresion == NULL) return;

    msj_consola_t msj;
    va_list args;
    va_start(args, formato);
    vsnprintf(msj.texto, sizeof(msj.texto), formato, args);
    va_end(args);

    xQueueSend(q_impresion, &msj, pdMS_TO_TICKS(10));
}

// TAREA GUARDIANA: Manejo de UART0 (PC) y UART1 (EGB)
void vTareaGuardianaConsola(void *pvParameters) {
    msj_consola_t msj_recibido;
    char buffer_rx[64];
    int idx = 0;

    while (1) {
        // 1. Envia datos pendientes hacia la PC por UART0
        if (xQueueReceive(q_impresion, &msj_recibido, pdMS_TO_TICKS(10)) == pdTRUE) {
            uart_write_bytes(UART_PYTHON, msj_recibido.texto, strlen(msj_recibido.texto));
        }

        // 2. Lee comandos enviados desde la PC por UART0 (Terminal VS Code)
        uint8_t c;
        int len = uart_read_bytes(UART_PYTHON, &c, 1, pdMS_TO_TICKS(5));
        if (len > 0) {
            if (c == '\n' || c == '\r') {
                if (idx > 0) {
                    buffer_rx[idx] = '\0';
                    char respuesta[64];

                    // --- PARSER DE COMANDOS ---
                    if (strncmp(buffer_rx, "SET:FREQ:", 9) == 0) {
                        uint32_t val = (uint32_t)atoi(&buffer_rx[9]);
                        if (val >= 1 && val <= 1000000) {
                            ad9833_set_frequency(val);
                            snprintf(respuesta, sizeof(respuesta), "OK:FREQ:%lu\r\n", val);
                        } else {
                            snprintf(respuesta, sizeof(respuesta), "ERR:OUT_OF_RANGE\r\n");
                        }
                    }
                    else if (strncmp(buffer_rx, "SET:F_INI:", 10) == 0) {
                        uint32_t val = (uint32_t)atoi(&buffer_rx[10]);
                        if (val >= 1 && val < f_fin_actual) {
                            f_ini_actual = val;
                            snprintf(respuesta, sizeof(respuesta), "OK:F_INI:%lu\r\n", val);
                        } else {
                            snprintf(respuesta, sizeof(respuesta), "ERR:OUT_OF_RANGE\r\n");
                        }
                    }
                    else if (strncmp(buffer_rx, "SET:F_FIN:", 10) == 0) {
                        uint32_t val = (uint32_t)atoi(&buffer_rx[10]);
                        if (val > f_ini_actual && val <= 1000000) {
                            f_fin_actual = val;
                            snprintf(respuesta, sizeof(respuesta), "OK:F_FIN:%lu\r\n", val);
                        } else {
                            snprintf(respuesta, sizeof(respuesta), "ERR:OUT_OF_RANGE\r\n");
                        }
                    }
                    else if (strncmp(buffer_rx, "SET:PASO:", 9) == 0) {
                        uint32_t val = (uint32_t)atoi(&buffer_rx[9]);
                        if (val >= 1 && val <= 100000) {
                            paso_actual = val;
                            snprintf(respuesta, sizeof(respuesta), "OK:PASO:%lu\r\n", val);
                        } else {
                            snprintf(respuesta, sizeof(respuesta), "ERR:OUT_OF_RANGE\r\n");
                        }
                    }
                    else if (strncmp(buffer_rx, "SET:TIEMPO:", 11) == 0) {
                        uint32_t val = (uint32_t)atoi(&buffer_rx[11]);
                        if (val >= 10 && val <= 10000) {
                            tiempo_actual = val;
                            snprintf(respuesta, sizeof(respuesta), "OK:TIEMPO:%lu\r\n", val);
                        } else {
                            snprintf(respuesta, sizeof(respuesta), "ERR:OUT_OF_RANGE\r\n");
                        }
                    }
                    else if (strcmp(buffer_rx, "GET:STATUS") == 0) {
                        snprintf(respuesta, sizeof(respuesta), "STATUS:OK\r\n");
                    }
                    else if (strcmp(buffer_rx, "GET:F_INI") == 0) {
                        snprintf(respuesta, sizeof(respuesta), "F_INI:%lu\r\n", f_ini_actual);
                    }
                    else if (strcmp(buffer_rx, "GET:F_FIN") == 0) {
                        snprintf(respuesta, sizeof(respuesta), "F_FIN:%lu\r\n", f_fin_actual);
                    }
                    else if (strcmp(buffer_rx, "GET:PASO") == 0) {
                        snprintf(respuesta, sizeof(respuesta), "PASO:%lu\r\n", paso_actual);
                    }
                    else if (strcmp(buffer_rx, "GET:TIEMPO") == 0) {
                        snprintf(respuesta, sizeof(respuesta), "TIEMPO:%lu\r\n", tiempo_actual);
                    }
                    else {
                        snprintf(respuesta, sizeof(respuesta), "ERR:UNKNOWN_CMD\r\n");
                    }

                    // Devuelve la respuesta a la PC por UART0
                    uart_write_bytes(UART_PYTHON, respuesta, strlen(respuesta));
                    idx = 0;
                }
            } else if (idx < sizeof(buffer_rx) - 1) {
                buffer_rx[idx++] = (char)c;
            }
        }
    }
}
