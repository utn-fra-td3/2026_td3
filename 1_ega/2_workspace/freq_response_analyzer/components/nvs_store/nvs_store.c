// --- Includes ---
#include "nvs_store.h"
#include "nvs_flash.h"
#include "esp_log.h"

// --- Defines privados ---
#define NVS_STORE_NAMESPACE "sweep_cfg"
#define NVS_STORE_KEY "config"

// --- Variables privadas ---
static const char *TAG = "nvs_store";
static nvs_handle_t nvs_store_handle;

// --- Prototipos privados ---
static void nvs_store_init(void);
static void guardar_config(const sweep_config_t *config);
static void cargar_config(void);
static void enviar_config_a_menu(const sweep_config_t *config);

// --- Funciones ---

void task_nvs(void *pvParameters)
{
    nvs_store_init();

    nvs_cmd_msg_t cmd;
    while (1)
    {
        if (xQueueReceive(queue_nvs_cmd, &cmd, portMAX_DELAY) == pdTRUE)
        {
            switch (cmd.cmd)
            {
            case NVS_CMD_SAVE:
                guardar_config(&cmd.config);
                break;
            case NVS_CMD_LOAD:
                cargar_config();
                break;
            }
        }
    }
}

static void nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // La partición NVS está llena o tiene formato incompatible, se borra y se inicializa
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(nvs_open(NVS_STORE_NAMESPACE, NVS_READWRITE, &nvs_store_handle));

    ESP_LOGI(TAG, "NVS inicializado");
}

static void guardar_config(const sweep_config_t *config)
{
    esp_err_t err = nvs_set_blob(nvs_store_handle, NVS_STORE_KEY, config, sizeof(*config));
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_store_handle);
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "no se pudo guardar la configuracion: %s", esp_err_to_name(err));
    }
}

static void cargar_config(void)
{
    sweep_config_t config;
    size_t size = sizeof(config);
    esp_err_t err = nvs_get_blob(nvs_store_handle, NVS_STORE_KEY, &config, &size);

    if (err == ESP_OK)
    {
        enviar_config_a_menu(&config);
    }
    else if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "no hay configuracion guardada, se mantienen los valores por defecto");
    }
    else
    {
        ESP_LOGW(TAG, "no se pudo leer la configuracion: %s", esp_err_to_name(err));
    }
}

static void enviar_config_a_menu(const sweep_config_t *config)
{
    uint32_t valores[] = {config->frec_inicio, config->frec_final, config->puntos, config->tiempo};

    for (sweep_param_e param = 0; param <= 3; param++)
    {
        menu_event_msg_t ev = {
            .type = MENU_EVT_CONFIG_SET,
            .param = param,
            .value = valores[param],
        };
        xQueueSend(queue_menu_events, &ev, portMAX_DELAY);
    }
}
