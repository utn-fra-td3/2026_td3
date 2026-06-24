#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "config.h"
#include "uart_comm.h"


#define SW_RUNOFF 0

SemaphoreHandle_t sem_runoff = NULL;

uint8_t med_state = false;

void IRAM_ATTR gpio_runoff(void *param)
{
    BaseType_t woken = pdFALSE;

    xSemaphoreGiveFromISR(sem_runoff, &woken);

    if (woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

void task_runoff(void *param)
{
    while (1)
    {
        if (xSemaphoreTake(sem_runoff, portMAX_DELAY) == pdTRUE)
        {
            vTaskDelay(pdMS_TO_TICKS(50)); // antirebote

            if (gpio_get_level(SW_RUNOFF) == 0)
            {
                config_t config_leida;
                intro_t dato_intro;

                if (xQueuePeek(config, &config_leida, pdMS_TO_TICKS(100)) == pdTRUE) 
                {
                    if (config_leida.estado == med_stateoff_config)
                    {
                        dato_intro = 1;
                    }
                    else
                    {
                        dato_intro = 2;
                    }

                    xQueueSend(intro, &dato_intro, portMAX_DELAY);
                }

                while (gpio_get_level(SW_RUNOFF) == 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
    }
}

void app_main(void)
{
    config_init();
    uart_comm_init();

    sem_runoff = xSemaphoreCreateBinary();

    if (sem_runoff == NULL)
    {
        // No se pudo crear el semáforo
        return;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << SW_RUNOFF,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1));
    ESP_ERROR_CHECK(gpio_isr_handler_add(SW_RUNOFF, gpio_runoff, NULL));

    xTaskCreate(task_runoff, "task_runoff", 2048, NULL, 2, NULL);
}