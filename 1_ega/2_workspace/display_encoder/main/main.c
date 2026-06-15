#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "ssd1306.h"

static const char *TAG = "OSCILOSCOPIO";

#define EXAMPLE_PCNT_HIGH_LIMIT 100
#define EXAMPLE_PCNT_LOW_LIMIT  -100

#define EXAMPLE_EC11_GPIO_A 4
#define EXAMPLE_EC11_GPIO_B 6

// Cola global para comunicar el encoder con la tarea del menú
QueueHandle_t encoder_queue = NULL;

/* Estructura para pasar múltiples parámetros a la tarea */
typedef struct {
    SSD1306_t *display;
    pcnt_unit_handle_t pcnt_unit;
} menu_params_t;

/* Prototipos */
void menu_principal_task(void *pvParameters);

void menu_principal_task(void *pvParameters)
{
    menu_params_t *params = (menu_params_t *)pvParameters;
    SSD1306_t *dev = params->display;
    pcnt_unit_handle_t pcnt_unit = params->pcnt_unit;

    int opcion = 0;
    int ultimo_conteo = 0;
    int conteo_actual = 0;

    // Dibujar el menú por primera vez
    ssd1306_clear_screen(dev, false);

    while (1)
    {
        // Obtenemos el conteo actual del hardware del encoder
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &conteo_actual));

        // Cada "clic" físico del encoder suele equivaler a 2 o 4 pasos en el contador
        // Dependiendo de tu encoder, puedes ajustar este valor (ej: cambiar a 2 o 4)
        int diferencia = conteo_actual - ultimo_conteo;

        if (diferencia >= 4) // Giro a la derecha -> Baja en el menú
        {
            opcion++;
            if (opcion > 4) opcion = 0; // Vuelve al inicio si se pasa
            ultimo_conteo = conteo_actual;
            ssd1306_clear_screen(dev, false);
        }
        else if (diferencia <= -4) // Giro a la izquierda -> Sube en el menú
        {
            opcion--;
            if (opcion < 0) opcion = 4; // Vuelve al final si baja de 0
            ultimo_conteo = conteo_actual;
            ssd1306_clear_screen(dev, false);
        }

        /* Opcional: Si el encoder tiene pulsador integrado (Push Button), 
           podrías leerlo aquí para "Seleccionar" la opción (sustituyendo a la tecla 'N').
        */

        /* Dibujar textos estáticos */
        ssd1306_display_text(dev, 0, "OSCILOSCOPIO", 13, false);
        ssd1306_display_text(dev, 2, " Tiempo/div", 11, false);
        ssd1306_display_text(dev, 3, " Amplitud/div", 13, false);
        ssd1306_display_text(dev, 4, " Trigger", 8, false);

        /* Invertir color de la opción seleccionada actualmente */
        switch (opcion)
        {
            case 0:
                ssd1306_display_text(dev, 2, " Tiempo/div", 11, true);
                break;
            case 1:
                ssd1306_display_text(dev, 3, " Amplitud/div", 13, true);
                break;
            case 2:
                ssd1306_display_text(dev, 4, " Trigger", 8, true);
                break;
        }

        ssd1306_show_buffer(dev);

        // Retardo para no saturar la CPU y dar estabilidad al refresco de pantalla
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static bool example_pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}

void app_main(void)
{
    // 1. Configurar Pines del Encoder
    gpio_set_pull_mode(EXAMPLE_EC11_GPIO_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(EXAMPLE_EC11_GPIO_B, GPIO_PULLUP_ONLY);

    // 2. Inicializar Unidad PCNT
    ESP_LOGI(TAG, "Instalando unidad PCNT");
    pcnt_unit_config_t unit_config = {
        .high_limit = EXAMPLE_PCNT_HIGH_LIMIT,
        .low_limit = EXAMPLE_PCNT_LOW_LIMIT,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // 3. Filtro de Ruido (Filtro Glitch)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 2000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 4. Configurar Canales del Encoder (Modo Cuadratura)
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = EXAMPLE_EC11_GPIO_A,
        .level_gpio_num = EXAMPLE_EC11_GPIO_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = EXAMPLE_EC11_GPIO_B,
        .level_gpio_num = EXAMPLE_EC11_GPIO_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // 5. Watchpoints y Callbacks
    int watch_points[] = {EXAMPLE_PCNT_LOW_LIMIT, -50, 0, 50, EXAMPLE_PCNT_HIGH_LIMIT};
    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++) {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
    }
    pcnt_event_callbacks_t cbs = {
        .on_reach = example_pcnt_on_reach,
    };
    encoder_queue = xQueueCreate(10, sizeof(int));
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, encoder_queue));

    // 6. Iniciar Hardware PCNT
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // 7. Inicializar Pantalla SSD1306
    static SSD1306_t dev;
#if CONFIG_I2C_INTERFACE
    ESP_LOGI(TAG, "Iniciando interfaz I2C");
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
#endif
    ssd1306_init(&dev, 128, 64);
    ssd1306_contrast(&dev, 0xff);
    ssd1306_clear_screen(&dev, false);

    // 8. Crear Estructura de parámetros y lanzar la Tarea del Menú
    static menu_params_t menu_parameters;
    menu_parameters.display = &dev;
    menu_parameters.pcnt_unit = pcnt_unit;

    xTaskCreate(
        menu_principal_task,
        "MENU",
        4096,
        &menu_parameters, // Pasamos la pantalla y el encoder juntos
        5,
        NULL);

    // El bucle de app_main se queda liberando procesamiento
    while (1)
    {
        vTaskDelay(portMAX_DELAY);
    }
}