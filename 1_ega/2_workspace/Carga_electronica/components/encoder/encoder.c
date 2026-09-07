#include "encoder.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include <esp_timer.h>
#include "lvgl.h"
#include "pin_setting.h"

static const char *TAG = "ENCODER";

static pcnt_unit_handle_t pcnt_unit = NULL;
static lv_indev_t * encoder_indev = NULL;
static void (*long_press_cb)(void) = NULL;

void encoder_set_long_press_cb(void (*cb)(void)) {
    long_press_cb = cb;
}

static void encoder_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    // Read PCNT count
    int count = 0;
    pcnt_unit_get_count(pcnt_unit, &count);
    
    // Calculate difference since last reported count
    static int last_reported_count = 0;
    int diff = count - last_reported_count;
    
    // Dividimos por 4 porque usamos decodificación en cuadratura completa (4 cuentas por click)
    int steps = diff / 4; 
    
    if (steps != 0) {
        data->enc_diff = steps;
        last_reported_count += (steps * 4); // Solo actualizamos lo que realmente reportamos
    } else {
        data->enc_diff = 0;
    }
    
    // Anti-rebote por software para el botón (20 ms)
    static uint32_t last_btn_time = 0;
    static int last_btn_state = 1;
    static int current_btn_state = 1;
    
    int raw_state = gpio_get_level(ENCODER_PIN_SW);
    uint32_t now = esp_timer_get_time() / 1000; // ms

    if (raw_state != last_btn_state) {
        last_btn_time = now;
        last_btn_state = raw_state;
    }

    if ((now - last_btn_time) > 20) { 
        current_btn_state = raw_state;
    }

    static uint32_t btn_press_start_time = 0;
    static bool long_press_handled = false;

    // Lógica del botón con detección de 2 segundos y click corto
    if (current_btn_state == 0) { // Presionado
        data->state = LV_INDEV_STATE_RELEASED; // Ocultamos a LVGL para evitar toggle de Edit Mode por defecto
        
        if (btn_press_start_time == 0) {
            btn_press_start_time = now;
            long_press_handled = false;
        } else if (!long_press_handled && (now - btn_press_start_time >= 2000)) {
            // Mantenido por 2 segundos
            long_press_handled = true;
            if (long_press_cb) {
                long_press_cb();
            }
        }
    } else { // Liberado
        data->state = LV_INDEV_STATE_RELEASED;
        if (btn_press_start_time != 0) {
            uint32_t hold_time = now - btn_press_start_time;
            if (!long_press_handled && hold_time > 50 && hold_time < 2000) {
                ESP_LOGI(TAG, "Botón presionado corto. Simulando CLICK.");
                lv_obj_t * focused = lv_group_get_focused(lv_group_get_default());
                if (focused) {
                    lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                }
            }
            btn_press_start_time = 0;
        }
    }
}

void encoder_init(void)
{
    ESP_LOGI(TAG, "Inicializando Encoder PCNT en pines A=%d, B=%d", ENCODER_PIN_A, ENCODER_PIN_B);

    // 1. Configurar la unidad principal del contador de pulsos (PCNT)
    pcnt_unit_config_t unit_config = {
        .high_limit = 10000,
        .low_limit = -10000,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Filtro contra ruidos de rebote mecánico (PCNT)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, 
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 2. Configurar el canal A
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_PIN_A,
        .level_gpio_num = ENCODER_PIN_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // 3. Configurar el canal B
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_PIN_B,
        .level_gpio_num = ENCODER_PIN_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // 4. Configurar acciones de cuenta (cuadratura)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // 5. Iniciar PCNT
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // 6. Configurar el botón del encoder (con Pull-Up interno)
    ESP_LOGI(TAG, "Inicializando Botón en pin SW=%d", ENCODER_PIN_SW);
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << ENCODER_PIN_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    // 7. Configurar el dispositivo de entrada en LVGL
    ESP_LOGI(TAG, "Registrando Encoder en LVGL");
    encoder_indev = lv_indev_create();
    lv_indev_set_type(encoder_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(encoder_indev, encoder_read_cb);

    // 8. Crear un grupo por defecto y asignárselo al encoder
    lv_group_t * g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(encoder_indev, g);
    
    // Forzamos el modo edición para que al girar el encoder se mueva el roller directamente
    lv_group_set_editing(g, true);
}