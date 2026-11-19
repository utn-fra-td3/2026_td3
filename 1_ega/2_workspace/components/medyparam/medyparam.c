#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "medyparam.h"
#include "trigger.h"
#include "config.h"
#include "adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifndef CENTRO_V
#define CENTRO_V 1.57f
#endif

#define FRAME_MUESTRAS   256    // Número de muestras que se leen del ADC en cada llamada a adc_leer_muestras()

#define MARGEN           150
#define CAPTURA_TOTAL    (NUM_MUESTRAS + MARGEN)
#define HISTERESIS_V     0.05f   // Valor de histéresis para el armado del trigger (V). La señal debe alejarse de este valor antes de aceptar un cruce.

QueueHandle_t enviar = NULL;

/* Doble buffer: mientras uart_comm copia una captura, la siguiente se arma
 * en el otro buffer. Antes era uno solo y la trama se podia mezclar con la
 * captura siguiente mientras se enviaba. */
static medyparam_t buffers[2];
static uint8_t buf_actual = 0;
#define datos (buffers[buf_actual])
static trigger_mv_t trigger_mv_actual = 0;
static adc_muestra_t frame[FRAME_MUESTRAS];
static float cap[CAPTURA_TOTAL];

static volatile int pot_raw_actual = 2048;

static float factor_por_escala(uint8_t escala)
{
    if (escala == escala_500mv_div) return 2.5f;
    return 1.0f;
}

static uint8_t avg_por_base(uint8_t base)
{
    if (base == base_0p4ms_div) return 2;
    if (base == base_1p0ms_div) return 5;
    return 1;
}

static float escalar(uint16_t raw, float factor)
{
    float v = (raw * 3.3f) / 4095.0f;
    return (v - CENTRO_V) * factor;
}


static int buscar_cruce(const float *buf, int rango, float nivel, uint8_t flanco)
{
    bool armado = false;

    for (int i = 1; i < rango; i++)
    {
        if (flanco == flanco_ascendente)
        {
            if (buf[i] < nivel - HISTERESIS_V) armado = true;
            if (armado && buf[i - 1] < nivel && buf[i] >= nivel) return i;
        }
        else
        {
            if (buf[i] > nivel + HISTERESIS_V) armado = true;
            if (armado && buf[i - 1] > nivel && buf[i] <= nivel) return i;
        }
    }
    return -1;
}

int medyparam_trigger_mv_escalado(uint8_t escala)
{
    float v = (pot_raw_actual * 3.3f) / 4095.0f;
    return (int)((v - CENTRO_V) * factor_por_escala(escala) * 1000.0f);
}

void medyparam_init(void)
{
    enviar = xQueueCreate(1, sizeof(medyparam_t *));
    xTaskCreate(task_medyparam, "task_medyparam", 8192, NULL, 5, NULL);
}

void task_medyparam(void *param)
{
    (void)param;

    trigger_t dato_trigger;
    medyparam_t *puntero;

    bool capturando = false;
    int puntos = 0;
    uint32_t acc = 0;       // Acumulador de muestras para promediar
    uint8_t acc_n = 0;      // Número de muestras acumuladas
    uint8_t avg = 1;        // Número de muestras a promediar según la base de tiempo seleccionada
    float factor = 1.0f;    // Factor de escalado según la escala vertical seleccionada

    while (trigger == NULL || enviar == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    adc_start();

    while (1)
    {
        uint16_t leidas = 0;
        esp_err_t ret = adc_leer_muestras(frame, FRAME_MUESTRAS, &leidas);

        // Se procesan las muestras leídas del ADC
        if (ret == ESP_OK)
        {
            for (uint16_t i = 0; i < leidas; i++)
            {
                if (frame[i].canal == ADC_CANAL_TRIGGER)
                {
                    pot_raw_actual = frame[i].raw;
                    trigger_mv_actual = (trigger_mv_t)((pot_raw_actual * 3300) / 4095);
                    xQueueOverwrite(trigger_mv, &trigger_mv_actual);
                }
                else if (frame[i].canal == ADC_CANAL_SENAL)
                {   
                    if (capturando)
                    {
                        acc += frame[i].raw;
                        acc_n++;
                        // Se promedian las muestras según la base de tiempo seleccionada
                        if (acc_n >= avg)
                        {
                            uint16_t raw_prom = (uint16_t)(acc / avg);

                            if (puntos < CAPTURA_TOTAL)
                            {
                                cap[puntos] = escalar(raw_prom, factor);
                            }
                            puntos++;
                            acc = 0;
                            acc_n = 0;
                            // Se verifica si ya se han capturado suficientes muestras para enviar el paquete de datos
                            if (puntos >= CAPTURA_TOTAL)
                            {
                                datos.nivel_trigger = escalar((uint16_t)pot_raw_actual, factor);

                                int cruce = buscar_cruce(cap, MARGEN,
                                                         datos.nivel_trigger,
                                                         datos.config_usada.flanco);
                                if (cruce < 0) cruce = 0;   

                                for (int k = 0; k < NUM_MUESTRAS; k++)
                                {
                                    datos.muestras[k] = cap[cruce + k];
                                }

                                puntero = &datos;
                                xQueueOverwrite(enviar, &puntero);
                                buf_actual ^= 1;   // la proxima captura va al otro buffer

                                capturando = false;

                                if (xQueuePeek(trigger, &dato_trigger, 0) == pdTRUE)
                                {
                                    dato_trigger.disparar_medicion = 0;
                                    xQueueOverwrite(trigger, &dato_trigger);
                                }
                            }
                        }
                    }
                }
            }
        }
        else if (ret == ESP_ERR_INVALID_STATE)
        {
            adc_stop();
            adc_start();
        }

        // Si no se está capturando, se revisa si hay un trigger pendiente.
        if (!capturando)
        {
            if (xQueuePeek(trigger, &dato_trigger, 0) == pdTRUE)
            {
                if (dato_trigger.disparar_medicion == 1 && dato_trigger.config_trigger.estado == med_staterun_config)   
                {
                    datos.config_usada = dato_trigger.config_trigger;
                    factor = factor_por_escala(datos.config_usada.escala_vertical);
                    avg = avg_por_base(datos.config_usada.base_tiempo);

                    capturando = true;
                    puntos = 0;
                    acc = 0;
                    acc_n = 0;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}