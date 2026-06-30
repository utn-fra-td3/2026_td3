#include <string.h>

#include "flash.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NAMESPACE   "osc"
#define NVS_KEY_CONFIG  "cfg"

void flash_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

void flash_task_start(void)
{
    xTaskCreate(task_flash, "task_flash", 4096, NULL, 1, NULL);
}

int flash_save_config(const config_t *cfg)
{
    if (cfg == NULL)
    {
        return 0;
    }

    config_t cfg_para_guardar = *cfg;
    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    err = nvs_set_blob(
        handle,
        NVS_KEY_CONFIG,
        &cfg_para_guardar,
        sizeof(config_t)
    );


    nvs_close(handle);

    return (err == ESP_OK);
}

int flash_load_config(config_t *cfg)
{

    nvs_handle_t handle;
    size_t size = sizeof(config_t);

    esp_err_t err = nvs_open(
        NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    err = nvs_get_blob(
        handle,
        NVS_KEY_CONFIG,
        cfg,
        &size
    );

    nvs_close(handle);

    if (size != sizeof(config_t))
    {
        return 0;
    }

    return 1;
}

void task_flash(void *param)
{
    config_t config_leida;
    config_t ultima_guardada;

    uint8_t hay_ultima_guardada = 0;

    while (config == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (flash_load_config(&ultima_guardada))
    {
        hay_ultima_guardada = 1;
    }

    while (1)
    {
        if (xQueuePeek(config, &config_leida, pdMS_TO_TICKS(500)) == pdTRUE)
        {

            config_leida.estado = med_stateoff_config; // Forzamos a OFF para comparar

            if (!hay_ultima_guardada || memcmp(&config_leida, &ultima_guardada, sizeof(config_t)) != 0) // Si no hay ultima guardada o si hay cambios
            {
                if (flash_save_config(&config_leida))
                {
                    ultima_guardada = config_leida;
                    ultima_guardada.estado = med_stateoff_config;

                    hay_ultima_guardada = 1;
                }
            }
        }

        /*
         * Revisa cada 1 segundo.
         * Solo escribe si hubo cambios.
         */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}