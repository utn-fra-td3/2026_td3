#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

QueueHandle_t intro = NULL;
QueueHandle_t config = NULL;

void config_init(void)
{
    intro = xQueueCreate(10, sizeof(intro_t));
    config = xQueueCreate(1, sizeof(config_t));

    if (intro == NULL || config == NULL)
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
        .escala_vertical = escala_5v_div,
        .base_tiempo = base_1ms_div,
        .flanco = flanco_ascendente,
        .nivel_trigger = 1000
    };

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
                    if (config_actual.escala_vertical == escala_1v_div)
                    {
                        config_actual.escala_vertical = escala_5v_div;
                    }
                    else
                    {
                        config_actual.escala_vertical = escala_1v_div;
                    }
                    break;

                case 4:           
                    if (config_actual.base_tiempo == base_1ms_div)
                    {
                        config_actual.base_tiempo = base_2ms_div;
                    }
                    else if (config_actual.base_tiempo == base_2ms_div)
                    {
                        config_actual.base_tiempo = base_5ms_div;
                    }
                    else
                    {
                        config_actual.base_tiempo = base_1ms_div;
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
                    // Mostrar configuración actual
                    break;

                default:
                    // comando no reconocido, no hago nada
                    break;
            }

            xQueueOverwrite(config, &config_actual);
        }
    }
}