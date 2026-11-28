#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "tipos_sistema.h"
#include "encoder.h"
#include "guardiana.h"
#include "ad9833.h"
#include "oled_ssd1306.h"

extern void vTareaMenu(void *pvParameters);
extern void vTareaAdquisicion(void *pvParameters);
extern void vTareaProcesamiento(void *pvParameters);

static colas_sistema_t colas;
volatile uint32_t idle_ticks = 0;

bool vApplicationIdleHook(void) {
    idle_ticks++;
    return true;
}

void vTareaSupervisor(void *pvParameters) {
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        imprimir_consola("\n[SUPERVISOR] -> Sistema OK. Ciclos Idle: %lu\n", idle_ticks);
        idle_ticks = 0;
    }
}

void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    oled_init();
    oled_clear();
    oled_update();

    ad9833_init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    guardiana_init();

    colas.q_encoder = xQueueCreate(10, sizeof(encoder_event_t));
    colas.q_comandos_barrido = xQueueCreate(2, sizeof(param_barrido_t));
    colas.q_datos_crudos = xQueueCreate(10, sizeof(datos_crudos_t)); 

    encoder_init(colas.q_encoder);

    // Lanzamiento de Tareas
    xTaskCreate(vTareaGuardianaConsola, "Guardiana", 2048, NULL, 6, NULL);
    xTaskCreate(vTareaMenu, "Menu", 4096, (void*)&colas, 5, NULL);
    xTaskCreate(vTareaAdquisicion, "AdqADC", 4096, (void*)&colas, 4, NULL);
    xTaskCreate(vTareaProcesamiento, "Procesam", 4096, (void*)&colas, 3, NULL);
    xTaskCreate(vTareaSupervisor, "Superv", 2048, NULL, 1, NULL);
}