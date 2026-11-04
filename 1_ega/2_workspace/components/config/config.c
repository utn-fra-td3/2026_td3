#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "flash.h"

QueueHandle_t intro = NULL;
QueueHandle_t config = NULL;
QueueHandle_t trigger_mv = NULL;

void config_init(void)
{
    intro = xQueueCreate(10, sizeof(intro_t));
    config = xQueueCreate(1, sizeof(config_t));
    trigger_mv = xQueueCreate(1, sizeof(trigger_mv_t));

    if (intro == NULL || config == NULL || trigger_mv == NULL)
    {
        // No se pudo crear una de las colas
        return;
    }

    xTaskCreate(task_config, "task_config", 4096, NULL, 3, NULL);
}

void task_config(void *param)
{
    intro_t comando_recibido; 

    config_t config_actual = {
        .estado = med_stateoff_config,
        .escala_vertical = escala_200mv_div,
        .base_tiempo = base_0p2ms_div,
        .flanco = flanco_ascendente,
        .nivel_trigger = 0
    };

    flash_load_config(&config_actual); // Cargar la configuración desde la memoria flash
    config_actual.estado = med_stateoff_config; // Arranco con en off
    config_actual.nivel_trigger = 0; // Inicializo el nivel de trigger a 0, ya que no se ha medido aún
    xQueueOverwrite(config, &config_actual); 

    while (1)
    {
        if (xQueueReceive(intro, &comando_recibido, portMAX_DELAY) == pdTRUE)
        {
            switch (comando_recibido)
            {
                case 1:
                    config_actual.estado = med_staterun_config;
                    break;

                case 2:
                    config_actual.estado = med_stateoff_config;
                    break;

                case 3:
                    if (config_actual.escala_vertical == escala_200mv_div)
                    {
                        config_actual.escala_vertical = escala_500mv_div;
                    }
                    else
                    {
                        config_actual.escala_vertical = escala_200mv_div;
                    }
                    break;

                case 4:           
                    if (config_actual.base_tiempo == base_0p2ms_div)
                    {
                        config_actual.base_tiempo = base_0p4ms_div;
                    }
                    else if (config_actual.base_tiempo == base_0p4ms_div)
                    {
                        config_actual.base_tiempo = base_1p0ms_div;
                    }
                    else
                    {
                        config_actual.base_tiempo = base_0p2ms_div;
                    }
                    break;

                case 5:
                    // Nivel trigger, falta esta medición
                    break;

                case 6:
                    if (config_actual.flanco == flanco_ascendente)
                    {
                        config_actual.flanco = flanco_descendente;
                    }
                    else
                    {
                        config_actual.flanco = flanco_ascendente;
                    }
                    break;

                case 7:
                // Mostrar configuración actual, esto se maneja en uart_comm.c
                    break;

                default:
                    // Comandos absolutos que llegan desde la Raspberry (uart_comm)
                    if (comando_recibido >= CMD_SET_ESCALA && comando_recibido <= CMD_SET_ESCALA + escala_500mv_div)
                    {
                        config_actual.escala_vertical = comando_recibido - CMD_SET_ESCALA;
                    }
                    else if (comando_recibido >= CMD_SET_BASE && comando_recibido <= CMD_SET_BASE + base_1p0ms_div)
                    {
                        config_actual.base_tiempo = comando_recibido - CMD_SET_BASE;
                    }
                    else if (comando_recibido >= CMD_SET_FLANCO && comando_recibido <= CMD_SET_FLANCO + flanco_descendente)
                    {
                        config_actual.flanco = comando_recibido - CMD_SET_FLANCO;
                    }
                    // cualquier otro valor: comando no reconocido, no hago nada
                    break;
            }

            xQueueOverwrite(config, &config_actual);
        }
        
        trigger_mv_t nuevo_trigger_mv;

        if (xQueueReceive(trigger_mv, &nuevo_trigger_mv, 0) == pdTRUE)
        {
            config_actual.nivel_trigger = nuevo_trigger_mv;
            xQueueOverwrite(config, &config_actual);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
    }
}