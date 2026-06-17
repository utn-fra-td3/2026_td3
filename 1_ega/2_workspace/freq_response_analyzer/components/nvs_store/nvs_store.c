// --- Includes ---
#include "nvs_store.h"
#include "nvs_flash.h"
#include "esp_log.h"

// --- Variables privadas ---
static const char *TAG = "nvs_store";

// --- Prototipos privados ---
static void nvs_store_init(void);
static void nvs_store_test(void); // TEST TEMPORAL

// --- Funciones ---

void task_nvs(void *pvParameters)
{
    nvs_store_init();

    while (1)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

static void nvs_store_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // La partición NVS está llena o tiene formato incompatible, se borrar y se inicializa
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);


    ESP_LOGI(TAG, "NVS inicializado");
}
