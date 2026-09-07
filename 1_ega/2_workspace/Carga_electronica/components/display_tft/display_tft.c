#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <sys/lock.h>
#include <sys/param.h>
#include <esp_timer.h>

#include "lvgl.h"
#include "ui.h"
#include "pin_setting.h"
#include "communication_setting.h"
#include "encoder.h"
#include "display_tft.h"
#include "system_manager.h"

extern QueueHandle_t sysman_queue;
extern QueueHandle_t display_queue;

// ============================================================================
// 1. DEFINICIONES Y VARIABLES GLOBALES DEL DISPLAY
// ============================================================================
#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIXEL_CLOCK_HZ 20 * 1000 * 1000 //20 MHz
#define LCD_CMD_BITS 8
#define LCD_PARAM_BITS 8
#define LVGL_TICK_PERIOD_MS 2

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t lcd_panel_handle = NULL;
static _lock_t lvgl_api_lock;
static const char *TAG_STARTUP = "STARTUP";
static const char *TAG_UI = "UI_APP";
static int startup_progress_x10 = 0; // 0 a 1000 que representa 0.0% a 100.0%

// ============================================================================
// 2. FUNCIONES DE UTILIDAD (LVGL LOCKS)
// ============================================================================
void lvgl_acquire(void)
{
    _lock_acquire(&lvgl_api_lock);
}

void lvgl_release(void)
{
    _lock_release(&lvgl_api_lock);
}

// ============================================================================
// 3. RUTINAS DE INICIALIZACIÓN DE HARDWARE (SPI + LCD + LVGL)
// ============================================================================
static void display_init (void)
{
	gpio_set_direction(PIN_NUM_LED, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_NUM_LED, 1);

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &lcd_panel_handle));

    esp_lcd_panel_reset(lcd_panel_handle);
    esp_lcd_panel_init(lcd_panel_handle);
    esp_lcd_panel_disp_on_off(lcd_panel_handle, true);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_port_update_callback(lv_display_t *disp)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISPLAY_ROTATION_90:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISPLAY_ROTATION_180:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISPLAY_ROTATION_270:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void display_and_lvgl_init(void)
{
    display_init();
    
    lv_init();

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);

    size_t draw_buffer_sz = LCD_H_RES * 20 * sizeof(lv_color16_t);
    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);

    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display, lcd_panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, lvgl_flush_cb);
    
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);
    lvgl_port_update_callback(display);

    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));
}

// ============================================================================
// 4. LÓGICA DE INTERFAZ DE USUARIO (EVENTOS Y ANIMACIONES)
// ============================================================================

static void startup_timer_cb(lv_timer_t *timer)
{
    startup_progress_x10 += 20;
    if (startup_progress_x10 >= 1000) {
        startup_progress_x10 = 1000;
        lv_timer_delete(timer);
        ESP_LOGI(TAG_STARTUP, "Carga completa. Transicionando a ui_Init");
        lv_screen_load(ui_Init);
    }
    if (ui_barra_de_carga) {
        lv_bar_set_value(ui_barra_de_carga, startup_progress_x10 / 10, LV_ANIM_OFF);
    }
}

static void display_setup_startup_bar(void)
{
    if (ui_barra_de_carga) {
        lv_bar_set_value(ui_barra_de_carga, 0, LV_ANIM_OFF);
    }
    lv_timer_create(startup_timer_cb, 100, NULL);
    ESP_LOGI(TAG_STARTUP, "Timer de progreso creado (20%%/seg)");
}

void actualizar_reloj_pantallas(const char * nueva_hora)
{
    lv_obj_t* relojes[] = {
        ui_hora1, ui_hora2, ui_hora3, ui_hora4, 
        ui_hora5, ui_hora6, ui_hora7, ui_hora8
    };
    
    for(int i = 0; i < 8; i++) {
        if(relojes[i] != NULL) {
            lv_obj_t * label = ui_comp_get_child(relojes[i], UI_COMP_HORA_HORA);
            if(label != NULL) {
                lv_label_set_text(label, nueva_hora);
            }
        }
    }
}

void Seleccionarmodo(lv_event_t * e)
{
    uint16_t selected = lv_roller_get_selected(ui_Modo_seleccionado);
    sysman_msg_t msg;
    msg.type = SYSMAN_MSG_MODE_CHANGE;

    if (selected == 0) { // CR
        msg.data.new_mode = MODE_CR;
        lv_screen_load(ui_Resistencia);
        lv_group_focus_obj(ui_bar_resistencia);
        lv_group_set_editing(lv_group_get_default(), true);
    } else if (selected == 1) { // CV
        msg.data.new_mode = MODE_CV;
        lv_screen_load(ui_Voltaje);
        lv_group_focus_obj(ui_bar_voltaje);
        lv_group_set_editing(lv_group_get_default(), true);
    } else if (selected == 2) { // CC
        msg.data.new_mode = MODE_CC;
        lv_screen_load(ui_Corriente);
        lv_group_focus_obj(ui_bar_corriente);
        lv_group_set_editing(lv_group_get_default(), true);
    }

    if (sysman_queue != NULL) {
        xQueueSend(sysman_queue, &msg, 0);
    }
}

extern lv_obj_t * ui_voltaje_set;      // Pantalla de voltaje
extern lv_obj_t * ui_voltaje_set1;     // Pantalla de corriente (Squareline lo nombró así)
extern lv_obj_t * ui_Resistencia_set;  // Pantalla de resistencia

static void on_slider_changed(lv_event_t * e)
{
    lv_obj_t * slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    
    sysman_msg_t msg;
    msg.type = SYSMAN_MSG_SETPOINT_CHANGE;

    if (slider == ui_bar_resistencia) {
        msg.type = SYSMAN_MSG_SETPOINT_CHANGE;
        msg.data.new_setpoint = (float)val; 
        if (ui_Resistencia_set) {
            lv_label_set_text_fmt(ui_Resistencia_set, "%ld Ohm", (long)val);
        }
    } 
    else if (slider == ui_bar_voltaje) {
        msg.type = SYSMAN_MSG_SETPOINT_CHANGE;
        msg.data.new_setpoint = (float)val / 100.0f; 
        if (ui_voltaje_set) {
            lv_label_set_text_fmt(ui_voltaje_set, "%ld.%02ld V", (long)(val / 100), (long)(val % 100));
        }
    } 
    else if (slider == ui_bar_corriente) {
        msg.type = SYSMAN_MSG_SETPOINT_CHANGE;
        msg.data.new_setpoint = (float)val / 1000.0f;
        if (ui_voltaje_set1) {
            lv_label_set_text_fmt(ui_voltaje_set1, "%ld.%03ld A", (long)(val / 1000), (long)(val % 1000));
        }
    }
    
    // -- LIMITES DE ALARMA --
    extern lv_obj_t * ui_voltaje_lim_set;
    extern lv_obj_t * ui_corr_limit_set;

    if (slider == ui_bar_voltaje_limit_set) {
        msg.type = SYSMAN_MSG_ALARM_LIMIT_CHANGE;
        msg.data.alarm_limit.type = ALARM_OVP;
        msg.data.alarm_limit.new_limit = (float)val / 100.0f; // Slider is 0-1500 for 15.00V? Assumed.
        if (ui_voltaje_lim_set) {
            lv_label_set_text_fmt(ui_voltaje_lim_set, "%ld.%02ld V", (long)(val / 100), (long)(val % 100));
        }
    }
    else if (slider == ui_bar_corr_limit) {
        msg.type = SYSMAN_MSG_ALARM_LIMIT_CHANGE;
        msg.data.alarm_limit.type = ALARM_OCP;
        msg.data.alarm_limit.new_limit = (float)val / 1000.0f; // Slider is 0-500 for 500mA
        if (ui_corr_limit_set) {
            lv_label_set_text_fmt(ui_corr_limit_set, "%ld.%03ld A", (long)(val / 1000), (long)(val % 1000));
        }
    }

    if (sysman_queue != NULL) {
        xQueueSend(sysman_queue, &msg, 0);
    }
}

void Avanzaropcion(lv_event_t * e)
{
}

static void ui_event_inicio_de_programa(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        uint16_t selected = lv_roller_get_selected(ui_inicio_de_programa);
        if(selected == 0) { // INICIAR
            sysman_msg_t msg = {.type = SYSMAN_MSG_ARM_ALARMS};
            xQueueSend(sysman_queue, &msg, 0);
            lv_screen_load(ui_Running);
        }
        else if(selected == 1) { // CONFIGURAR
            lv_screen_load(ui_Modo);
            lv_group_focus_obj(ui_Modo_seleccionado);
            lv_group_set_editing(lv_group_get_default(), true);
        }
        else if(selected == 2) { // ALARMAS
            lv_screen_load(ui_Voltaje_Limit_set);
            lv_group_focus_obj(ui_bar_voltaje_limit_set);
            lv_group_set_editing(lv_group_get_default(), true);
        }
    }
}

static void ui_event_start_running(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        sysman_msg_t msg = {.type = SYSMAN_MSG_ARM_ALARMS};
        xQueueSend(sysman_queue, &msg, 0);
        lv_screen_load(ui_Running);
    }
}

static void ui_event_bar_voltaje_limit_set(lv_event_t * e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if(event_code == LV_EVENT_CLICKED) {
        lv_screen_load(ui_Corriente_Limit_set);
        lv_group_focus_obj(ui_bar_corr_limit);
        lv_group_set_editing(lv_group_get_default(), true);
    }
}

static void on_encoder_long_press(void) {
    ESP_LOGI(TAG_UI, "Botón mantenido 2s. Volviendo al menú principal (ui_Init)");
    lv_screen_load(ui_Init);
    if (ui_inicio_de_programa) {
        lv_group_focus_obj(ui_inicio_de_programa);
        lv_group_set_editing(lv_group_get_default(), true);
    }
}

static void display_ui_app_init(void)
{
    encoder_set_long_press_cb(on_encoder_long_press);

    if (ui_inicio_de_programa) {
        lv_obj_add_event_cb(ui_inicio_de_programa, ui_event_inicio_de_programa, LV_EVENT_ALL, NULL);
        lv_group_add_obj(lv_group_get_default(), ui_inicio_de_programa);
    }
    if (ui_Modo_seleccionado) lv_group_add_obj(lv_group_get_default(), ui_Modo_seleccionado);
    if (ui_bar_resistencia) {
        lv_slider_set_range(ui_bar_resistencia, 1, 10000); // 1 a 10000 ohm
        lv_group_add_obj(lv_group_get_default(), ui_bar_resistencia);
        lv_obj_add_event_cb(ui_bar_resistencia, ui_event_start_running, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_bar_resistencia, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }
    if (ui_bar_voltaje) {
        lv_slider_set_range(ui_bar_voltaje, 0, 1200); // 0 a 12.00V
        lv_group_add_obj(lv_group_get_default(), ui_bar_voltaje);
        lv_obj_add_event_cb(ui_bar_voltaje, ui_event_start_running, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_bar_voltaje, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }
    if (ui_bar_corriente) {
        lv_slider_set_range(ui_bar_corriente, 0, 500); // 0 a 0.500A (500mA)
        lv_group_add_obj(lv_group_get_default(), ui_bar_corriente);
        lv_obj_add_event_cb(ui_bar_corriente, ui_event_start_running, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_bar_corriente, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }
    if (ui_bar_voltaje_limit_set) {
        lv_slider_set_range(ui_bar_voltaje_limit_set, 0, 1500); // 0 a 15.00V
        lv_group_add_obj(lv_group_get_default(), ui_bar_voltaje_limit_set);
        lv_obj_add_event_cb(ui_bar_voltaje_limit_set, ui_event_bar_voltaje_limit_set, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_bar_voltaje_limit_set, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }
    if (ui_bar_corr_limit) {
        lv_slider_set_range(ui_bar_corr_limit, 0, 500); // 0 a 0.500A
        lv_group_add_obj(lv_group_get_default(), ui_bar_corr_limit);
        lv_obj_add_event_cb(ui_bar_corr_limit, ui_event_start_running, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_bar_corr_limit, on_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

// ============================================================================
// 5. TAREA PRINCIPAL DEL DISPLAY
// ============================================================================

void task_display_update(void *pvParameters)
{
    // 1. Inicialización de Hardware y LVGL
    display_and_lvgl_init();
    
    // 2. Inicialización de la Interfaz y Eventos
    lvgl_acquire();
    ui_init();
    display_setup_startup_bar();
    encoder_init();
    display_ui_app_init();
    lvgl_release();

    // 3. Loop principal (Reemplaza a lvgl_port_task)
    uint32_t time_till_next_ms = 0;
    uint32_t time_threshold_ms = 2000 / CONFIG_FREERTOS_HZ;
    
    while (1) {
        lvgl_acquire();
        
        // --- Procesamiento de Mensajes Gráficos ---
        if (display_queue != NULL) {
            ui_update_t msg;
            while (xQueueReceive(display_queue, &msg, 0) == pdTRUE) {
                if (msg.source == UI_MSG_FROM_ADC) {
                    int voltage_centivolts = (int)(msg.voltage * 100.0f + 0.5f);
                    int current_milliamps = (int)(msg.current * 1000.0f + 0.5f);
                    int power_cent_watts = (int)(msg.voltage * msg.current * 100.0f + 0.5f);
                    if (uic_voltaje_actual) {
                        lv_label_set_text_fmt(uic_voltaje_actual, "%d.%02d V",
                                              voltage_centivolts / 100, voltage_centivolts % 100);
                    }
                    if (uic_corriente_actual) {
                        lv_label_set_text_fmt(uic_corriente_actual, "%d.%03d A",
                                              current_milliamps / 1000, current_milliamps % 1000);
                    }
                    if (uic_potencia_actual) {
                        lv_label_set_text_fmt(uic_potencia_actual, "%d.%02d W",
                                              power_cent_watts / 100, power_cent_watts % 100);
                    }
                } else if (msg.source == UI_MSG_FROM_SYSMAN) {
                    if (uic_modo_actual) {
                        if (msg.mode == MODE_CC) lv_label_set_text(uic_modo_actual, "CC");
                        else if (msg.mode == MODE_CV) lv_label_set_text(uic_modo_actual, "CV");
                        else if (msg.mode == MODE_CR) lv_label_set_text(uic_modo_actual, "CR");
                    }
                } else if (msg.source == UI_MSG_ALARM_TRIGGERED) {
                    // Turn on LED based on alarm type (mode field reused)
                    if ((alarm_type_t)msg.mode == ALARM_OVP && uic_Led_Voltaje) {
                        lv_obj_set_style_bg_color(uic_Led_Voltaje, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                    } else if ((alarm_type_t)msg.mode == ALARM_OCP && uic_Led_Corriente) {
                        lv_obj_set_style_bg_color(uic_Led_Corriente, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
                    }
                    if (uic_modo_actual) {
                        lv_label_set_text(uic_modo_actual, "ALARM!");
                    }
                }
            }
        }
        // ------------------------------------------

        time_till_next_ms = lv_timer_handler();
        lvgl_release();
        time_till_next_ms = MAX(time_till_next_ms, time_threshold_ms);
        vTaskDelay(pdMS_TO_TICKS(time_till_next_ms));
    }
}
