
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "uart_comm.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "medyparam.h"

#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     13          /* ESP TX -> Pi GPIO5 (pin fisico 29) */
#define UART_RX_PIN     14          /* ESP RX <- Pi GPIO4 (pin fisico 7)  */
#define UART_BAUDRATE   115200
#define UART_RX_BUF     512
#define UART_TX_BUF     2048        /* una trama entera entra sin bloquear */
#define LINEA_MAX       64

#define TRAMAS_MAX_POR_S 10

/* Codigos de error del protocolo */
#define ERR_VALOR        1          /* valor fuera de rango o mal formado */
#define ERR_SOLO_LECTURA 2          /* variable de solo lectura           */
#define ERR_DESCONOCIDO  3          /* variable o comando desconocido     */
#define ERR_NO_APLICADO  4          /* el cambio no se reflejo a tiempo   */

static const char *TAG = "uart_comm";

static bool stream_activo = true;


static void uart_print(const char *text)
{
    uart_write_bytes(UART_PORT, text, strlen(text));
}

static void responder(const char *var, int valor)
{
    char resp[LINEA_MAX];
    snprintf(resp, sizeof(resp), "%s=%d\n", var, valor);
    uart_print(resp);
}

static void responder_error(const char *var, int codigo)
{
    char resp[LINEA_MAX];
    snprintf(resp, sizeof(resp), "err %s %d\n", var, codigo);
    uart_print(resp);
}

void mostrar_config_actual(void)
{
    config_t c;

    if (xQueuePeek(config, &c, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        ESP_LOGI(TAG, "CONFIG: estado=%d escala=%d base=%d flanco=%d trigger=%d mV",
                 c.estado, c.escala_vertical, c.base_tiempo, c.flanco, c.nivel_trigger);
    }
    else
    {
        ESP_LOGE(TAG, "no se pudo leer la cola config");
    }
}


static const char *escala_a_str(uint8_t e)
{
    return (e == escala_500mv_div) ? "500mV" : "200mV";
}

static const char *base_a_str(uint8_t b)
{
    if (b == base_0p4ms_div) return "0.4ms";
    if (b == base_1p0ms_div) return "1ms";
    return "0.2ms";
}

// Formato: START\n
//          CONF:<escala>,<base>/div,<ASC|DESC>\n
//          TRIG:<nivel_trigger en V>\n
//          <NUM_MUESTRAS lineas, una muestra en V>\n
//          END\n
static void enviar_medicion_uart(medyparam_t *datos)
{
    char linea[48];

    if (datos == NULL)
    {
        return;
    }

    uart_print("START\n");

    snprintf(linea, sizeof(linea), "CONF:%s,%s/div,%s\n",
             escala_a_str(datos->config_usada.escala_vertical),
             base_a_str(datos->config_usada.base_tiempo),
             (datos->config_usada.flanco == flanco_ascendente) ? "ASC" : "DESC");
    uart_print(linea);

    snprintf(linea, sizeof(linea), "TRIG:%.2f\n", datos->nivel_trigger);
    uart_print(linea);

    for (uint16_t i = 0; i < NUM_MUESTRAS; i++)
    {
        snprintf(linea, sizeof(linea), "%.2f\n", datos->muestras[i]);
        uart_print(linea);
    }

    uart_print("END\n");
}


static bool leer_config(config_t *c)
{
    return xQueuePeek(config, c, pdMS_TO_TICKS(100)) == pdTRUE;
}

static bool valor_de(const char *var, int *valor)
{
    config_t c;

    if (strcmp(var, "stream") == 0) { *valor = stream_activo ? 1 : 0; return true; }

    if (!leer_config(&c)) return false;

    if (strcmp(var, "trig_mv") == 0)
    {
        *valor = medyparam_trigger_mv_escalado(c.escala_vertical);
        return true;
    }
    if (strcmp(var, "estado") == 0) { *valor = c.estado;          return true; }
    if (strcmp(var, "escala") == 0) { *valor = c.escala_vertical; return true; }
    if (strcmp(var, "base") == 0)   { *valor = c.base_tiempo;     return true; }
    if (strcmp(var, "flanco") == 0) { *valor = c.flanco;          return true; }

    return false;
}

static void cmd_get(const char *var)
{
    int v;

    if (valor_de(var, &v)) responder(var, v);
    else                   responder_error(var, ERR_DESCONOCIDO);
}

static void cmd_set(const char *var, int valor, bool valor_ok)
{
    intro_t comando;

    if (strcmp(var, "escala") == 0)
    {
        if (!valor_ok || valor < escala_200mv_div || valor > escala_500mv_div) { responder_error(var, ERR_VALOR); return; }
        comando = CMD_SET_ESCALA + valor;
    }
    else if (strcmp(var, "base") == 0)
    {
        if (!valor_ok || valor < base_0p2ms_div || valor > base_1p0ms_div) { responder_error(var, ERR_VALOR); return; }
        comando = CMD_SET_BASE + valor;
    }
    else if (strcmp(var, "flanco") == 0)
    {
        if (!valor_ok || valor < flanco_ascendente || valor > flanco_descendente) { responder_error(var, ERR_VALOR); return; }
        comando = CMD_SET_FLANCO + valor;
    }
    else if (strcmp(var, "estado") == 0 || strcmp(var, "trig_mv") == 0)
    {
        /* estado: RUN/STOP solo desde el boton fisico. trig_mv: sale del pote. */
        responder_error(var, ERR_SOLO_LECTURA);
        return;
    }
    else
    {
        responder_error(var, ERR_DESCONOCIDO);
        return;
    }

    if (xQueueSend(intro, &comando, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        responder_error(var, ERR_NO_APLICADO);
        return;
    }

    /* Feedback: se espera a ver el valor reflejado en la cola config */
    for (int i = 0; i < 20; i++)
    {
        int aplicado;
        if (valor_de(var, &aplicado) && aplicado == valor)
        {
            responder(var, valor);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    responder_error(var, ERR_NO_APLICADO);
}

static void cmd_exec(const char *accion)
{
    if (strcmp(accion, "stream_on") == 0)
    {
        stream_activo = true;
        responder("stream", 1);
    }
    else if (strcmp(accion, "stream_off") == 0)
    {
        stream_activo = false;
        responder("stream", 0);
    }
    else
    {
        responder_error(accion, ERR_DESCONOCIDO);
    }
}

static void procesar_linea(const char *linea)
{
    char verbo[8] = {0};
    char var[16]  = {0};     /* 16 = ESP32_VAR_NAME_MAX del driver */
    int  valor    = 0;
    int  n = sscanf(linea, "%7s %15s %d", verbo, var, &valor);

    ESP_LOGI(TAG, "RX: '%s'", linea);

    if (n >= 2 && strcmp(verbo, "get") == 0)       cmd_get(var);
    else if (n >= 2 && strcmp(verbo, "set") == 0)  cmd_set(var, valor, n == 3);
    else if (n >= 2 && strcmp(verbo, "exec") == 0) cmd_exec(var);
    else                                           responder_error("cmd", ERR_DESCONOCIDO);
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

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(task_uart, "task_uart", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "enlace con la Pi en UART1 (TX=%d, RX=%d, %d baud)",
             UART_TX_PIN, UART_RX_PIN, UART_BAUDRATE);
}

void task_uart(void *param)
{
    (void)param;

    uint8_t rx[64];
    char    linea[LINEA_MAX];
    size_t  len = 0;
    bool    descartando = false;
    medyparam_t *datos_recibidos = NULL;
    static medyparam_t copia;
    TickType_t ultimo_envio = 0;

    while (1)
    {
        /* 1) Comandos: se arma la linea byte a byte hasta el '\n' */
        int n = uart_read_bytes(UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(10));

        for (int i = 0; i < n; i++)
        {
            char c = (char)rx[i];

            if (c == '\r')
            {
                continue;
            }
            if (c == '\n')
            {
                if (!descartando && len > 0)
                {
                    linea[len] = '\0';
                    procesar_linea(linea);
                }
                len = 0;
                descartando = false;
            }
            else if (!descartando)
            {
                if (len < sizeof(linea) - 1) linea[len++] = c;
                else                         descartando = true;  /* linea demasiado larga */
            }
        }

        if (enviar != NULL && xQueueReceive(enviar, &datos_recibidos, 0) == pdTRUE)
        {
            TickType_t ahora = xTaskGetTickCount();

            if (stream_activo && (ahora - ultimo_envio) >= pdMS_TO_TICKS(1000 / TRAMAS_MAX_POR_S))
            {
                copia = *datos_recibidos;
                enviar_medicion_uart(&copia);
                ultimo_envio = ahora;
            }
        }
    }
}
