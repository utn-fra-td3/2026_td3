#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "uart_comm.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#define UART_PORT       UART_NUM_0
#define UART_BAUDRATE   115200
#define UART_BUF_SIZE   128

static void uart_print(const char *text)
{
    uart_write_bytes(UART_PORT, text, strlen(text));
}

static void limpiar_salto_linea(char *str)
{
    str[strcspn(str, "\r\n")] = '\0';
}

static void mostrar_comandos(void)
{
    uart_print(
        "\r\nComandos disponibles:\r\n"
        "1_RUN\r\n"
        "2_OFF\r\n"
        "3_Cambiar escala vertical\r\n"
        "4_Cambiar base de tiempo\r\n"
        "5_Ver nivel trigger\r\n"
        "6_Cambiar flanco\r\n"
        "7_Ver configuracion\r\n\r\n"
    );
}

void mostrar_config_actual(void)
{
    config_t config_leida; // Variable para almacenar la configuración leída de la cola
    char a[180];

    if (xQueuePeek(config, &config_leida, pdMS_TO_TICKS(100)) == pdTRUE)   //peek para no eliminar el item de la cola, solo leerlo
    {
        snprintf(a, sizeof(a),
                 "\r\nCONFIG ACTUAL\r\n"
                 "Estado: %d\r\n"
                 "Escala vertical: %d\r\n"
                 "Base de tiempo: %d\r\n"
                 "Nivel trigger: %d mV\r\n"
                 "Flanco: %d\r\n\r\n",
                 config_leida.estado,
                 config_leida.escala_vertical,
                 config_leida.base_tiempo,
                 config_leida.nivel_trigger,
                 config_leida.flanco);

        uart_print(a);
    }
    else
    {
        uart_print("ERROR: no se pudo leer la cola config\r\n");
    }
}

static void enviar_numero_a_intro(uint8_t numero)
{
    intro_t dato_intro = numero;

    if (xQueueSend(intro, &dato_intro, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        uart_print("ERROR: no se pudo enviar el comando a la cola intro\r\n");
    }
}

static void procesar_comando(char *cmd)
{
    limpiar_salto_linea(cmd); 

    if (strlen(cmd) == 0) 
    {
        return;
    }

    uint8_t numero = (uint8_t)atoi(cmd);

    if (numero >= 1 && numero <= 7)
    {
        enviar_numero_a_intro(numero);
        uart_print("\r\nComando enviado a intro: ");
        uart_write_bytes(UART_PORT, cmd, strlen(cmd));
        uart_print("\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        // mostrar_config_actual();
    }
    else
    {
        uart_print("\r\nComando no reconocido\r\n");
        mostrar_comandos();
    }
}

void uart_comm_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    uart_param_config(UART_PORT, &uart_config);

    xTaskCreate(task_uart, "task_uart", 4096, NULL, 2, NULL);
}

void task_uart(void *param)
{
    uint8_t data[UART_BUF_SIZE];

    mostrar_comandos();

    while (1)
    {
        int len = uart_read_bytes(UART_PORT, data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(100)); 

        if (len > 0)
        {
            data[len] = '\0'; // len es el número de bytes leídos
            procesar_comando((char *)data); // Procesa el comando recibido por UART
        }
    }
}