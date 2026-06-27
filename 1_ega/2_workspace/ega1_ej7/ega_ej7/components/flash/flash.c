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

    cfg_para_guardar.estado = med_stateoff_config; // Arrancar siempre en OFF

    nvs_handle_t handle;

    esp_err_t err = nvs_open(
        NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (err != ESP_OK)
    {
        return 0;
    }

    err = nvs_set_blob(
        handle,
        NVS_KEY_CONFIG,
        &cfg_para_guardar,
        sizeof(config_t)
    );

    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    return (err == ESP_OK);
}

int flash_load_config(config_t *cfg)
{
    if (cfg == NULL)
    {
        return 0;
    }

    nvs_handle_t handle;
    size_t size = sizeof(config_t);

    esp_err_t err = nvs_open(
        NVS_NAMESPACE,
        NVS_READONLY,
        &handle
    );

    if (err != ESP_OK)
    {
        return 0;
    }

    err = nvs_get_blob(
        handle,
        NVS_KEY_CONFIG,
        cfg,
        &size
    );

    nvs_close(handle);

    if (err != ESP_OK)
    {
        return 0;
    }

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

    if (flash_load_config(&ultima_guardada)) // Si hay una configuración guardada, la cargo como última guardada
    {
        hay_ultima_guardada = 1;
    }

    while (1)
    {
        if (xQueuePeek(config, &config_leida, pdMS_TO_TICKS(500)) == pdTRUE)
        {

            if (!hay_ultima_guardada ||
                memcmp(&config_leida, &ultima_guardada, sizeof(config_t)) != 0) // Si no hay última guardada o la configuración actual es diferente a la última guardada, guardo en flash
            {
                if (flash_save_config(&config_leida))
                {
                    ultima_guardada = config_leida;

                    ultima_guardada.estado = med_stateoff_config; // Arrancar siempre en OFF

                    hay_ultima_guardada = 1;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay para no saturar la CPU
    }
}