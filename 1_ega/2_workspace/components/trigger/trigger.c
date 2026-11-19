#include <string.h>
#include "trigger.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define GPIO_RUNSTOP       GPIO_NUM_5
#define GPIO_TRIGGER       GPIO_NUM_4

#define BOTON_PRESIONADO   1

QueueHandle_t trigger = NULL;

static SemaphoreHandle_t sem_runstop = NULL;
static SemaphoreHandle_t sem_trigger = NULL;

static void IRAM_ATTR isr_runstop(void *arg) // IRAM_ATTR para colocar la función en la memoria IRAM y que sea más rápida
{ 
    BaseType_t woken = pdFALSE; // Variable para indicar si se debe realizar un cambio de contexto

    xSemaphoreGiveFromISR(sem_runstop, &woken); // Libera el semáforo desde la ISR

    if (woken == pdTRUE) // Si se debe realizar un cambio de contexto, se llama a portYIELD_FROM_ISR() para forzar el cambio
    {
        portYIELD_FROM_ISR();
    }
}

static void IRAM_ATTR isr_trigger(void *arg)
{
    BaseType_t woken = pdFALSE;

    xSemaphoreGiveFromISR(sem_trigger, &woken);

    if (woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void trigger_gpio_init(void)
{
    gpio_config_t runstop_cfg = {
        .pin_bit_mask = 1ULL << GPIO_RUNSTOP,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };

    gpio_config(&runstop_cfg);

    gpio_config_t trigger_cfg = {
        .pin_bit_mask = 1ULL << GPIO_TRIGGER,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&trigger_cfg);

    esp_err_t err = gpio_install_isr_service(0);

    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE)
    {
        gpio_isr_handler_add(GPIO_RUNSTOP, isr_runstop, NULL);
        gpio_isr_handler_add(GPIO_TRIGGER, isr_trigger, NULL);
    }

    gpio_intr_disable(GPIO_TRIGGER); // Deshabilita la interrupción del trigger al inicio
}

static void aplicar_config_trigger(config_t config_actual)
{
    gpio_intr_disable(GPIO_TRIGGER);

    if (config_actual.flanco == flanco_ascendente)
    {
        gpio_set_intr_type(GPIO_TRIGGER, GPIO_INTR_POSEDGE);
    }
    else
    {
        gpio_set_intr_type(GPIO_TRIGGER, GPIO_INTR_NEGEDGE);
    }

    if (config_actual.estado == med_staterun_config)
    {
        gpio_intr_enable(GPIO_TRIGGER);
    }
}

static void cargar_trigger(config_t config_actual, uint8_t disparar)
{
    trigger_t dato_trigger;

    dato_trigger.config_trigger = config_actual;
    dato_trigger.disparar_medicion = disparar;

    xQueueOverwrite(trigger, &dato_trigger);
}

void trigger_init(void)
{
    sem_runstop = xSemaphoreCreateBinary();
    sem_trigger = xSemaphoreCreateBinary();

    trigger = xQueueCreate(1, sizeof(trigger_t));

    trigger_gpio_init();

    xTaskCreate(task_configtrigger, "task_configtrigger", 4096, NULL, 4, NULL);

    xTaskCreate(task_disparotrigger, "task_disparotrigger", 4096, NULL, 4, NULL);
}

void task_configtrigger(void *param)
{
    (void)param; // Evita advertencias de compilación sobre parámetros no utilizados

    config_t config_actual;
    config_t config_anterior;
    intro_t comando_recibido;

    uint8_t primera_vez = 1;

    while (config == NULL || intro == NULL || trigger == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (1)
    {
        if (xQueuePeek(config, &config_actual, pdMS_TO_TICKS(100)) == pdTRUE) // Lee la configuración actual de la cola config
        {
            if (primera_vez || memcmp(&config_actual, &config_anterior, sizeof(config_t)) != 0) // Compara la configuración actual con la anterior para detectar cambios
            {
                aplicar_config_trigger(config_actual); // Aplica la configuración del trigger según la configuración actual

                cargar_trigger(config_actual, 0); // Inicializa la cola trigger con la configuración actual y sin disparar la medición

                config_anterior = config_actual;
                primera_vez = 0;
            }
        }

        if (xSemaphoreTake(sem_runstop, 0) == pdTRUE) // espera a que se libere el semáforo del botón RUN/STOP
        {
            vTaskDelay(pdMS_TO_TICKS(50)); // antirebote

            if (gpio_get_level(GPIO_RUNSTOP) == BOTON_PRESIONADO)
            {
                if (xQueuePeek(config, &config_actual, pdMS_TO_TICKS(100)) == pdTRUE) 
                {
                    if (config_actual.estado == med_stateoff_config)
                    {
                        comando_recibido = 1;
                    }
                    else
                    {
                        comando_recibido = 2;
                    }

                    xQueueSend(intro, &comando_recibido, portMAX_DELAY);
                }

                if (xQueuePeek(config, &config_actual, pdMS_TO_TICKS(100)) == pdTRUE)
                {
                    aplicar_config_trigger(config_actual); // Aplica la configuración del trigger según la configuración actual

                    cargar_trigger(config_actual, 0); // Inicializa la cola trigger con la configuración actual y sin disparar la medición 

                    config_anterior = config_actual;
                    primera_vez = 0;
                }

                while (gpio_get_level(GPIO_RUNSTOP) == BOTON_PRESIONADO) // Espera a que se suelte el botón RUN/STOP
                {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void task_disparotrigger(void *param)
{
    (void)param;

    trigger_t dato_trigger;

    while (trigger == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (1)
    {
        xSemaphoreTake(sem_trigger, pdMS_TO_TICKS(500));

        if (xQueuePeek(trigger, &dato_trigger, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (dato_trigger.config_trigger.estado == med_staterun_config) // Solo dispara la medición si el estado es RUN
            {
                dato_trigger.disparar_medicion = 1;
            }
            else
            {
                dato_trigger.disparar_medicion = 0;
            }

            xQueueOverwrite(trigger, &dato_trigger);
        }
    }
}