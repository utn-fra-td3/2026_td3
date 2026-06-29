#include "display_tft.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"

// Drivers SPI y Pantalla
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

// Librerías Gráficas
#include "esp_lcd_st7789.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "TFT_DISPLAY";

// --- CONFIGURACIÓN DE PINES SPI ---
#define LCD_HOST                SPI2_HOST
#define PIN_NUM_SCLK            11
#define PIN_NUM_MOSI            12
#define PIN_NUM_MISO            -1  
#define PIN_NUM_LCD_DC          14
#define PIN_NUM_LCD_RST         13
#define PIN_NUM_LCD_CS          10
#define PIN_NUM_BK_LIGHT        21  

// --- CAMBIO A HORIZONTAL (APAISADO) ---
#define LCD_H_RES               320 // Ancho (Antes 240)
#define LCD_V_RES               240 // Alto  (Antes 320)

extern QueueHandle_t display_queue;

// --- PUNTEROS GLOBALES A LOS WIDGETS DE LVGL ---
static lv_obj_t *label_voltage;
static lv_obj_t *label_current;
static lv_obj_t *label_mode;
static lv_obj_t *label_setpoint;

static lv_style_t style_big_bold;
static lv_style_t style_label_medium;


static void display_init_hardware_and_ui(void) {
    ESP_LOGI(TAG, "Inicializando Bus SPI y Panel LCD (Modo Horizontal)...");

    gpio_reset_pin(PIN_NUM_BK_LIGHT);
    gpio_set_direction(PIN_NUM_BK_LIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_BK_LIGHT, 1);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t), 
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = 20 * 1000 * 1000, 
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, 
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // --- ROTACIÓN DE HARDWARE A HORIZONTAL ---
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true)); // Acuesta los ejes
    // Si la imagen sale al revés (patas para arriba), invierte estos true/false:
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false)); 

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Inicializando LVGL...");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG(); 
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * 40,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .flags = { .buff_dma = true }
    };
    lvgl_port_add_disp(&disp_cfg);

    if (lvgl_port_lock(0)) {
        
        lv_style_init(&style_big_bold);
        lv_style_set_text_font(&style_big_bold, &lv_font_montserrat_24); 
        lv_style_set_text_color(&style_big_bold, lv_color_hex(0xf8fafc));     

        lv_style_init(&style_label_medium);
        lv_style_set_text_font(&style_label_medium, &lv_font_montserrat_18);   
        lv_style_set_text_color(&style_label_medium, lv_color_hex(0x64748b)); 

        // Fondo por defecto (Monitor)
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0f172a), 0);

        label_mode = lv_label_create(lv_scr_act());
        lv_label_set_text(label_mode, "MODO: CC");
        lv_obj_add_style(label_mode, &style_label_medium, 0); 
        lv_obj_set_style_text_color(label_mode, lv_color_hex(0x3b82f6), 0); 
        lv_label_set_recolor(label_mode, true); // Permite tags HTML de colores
        lv_obj_align(label_mode, LV_ALIGN_TOP_MID, 0, 10);

        label_voltage = lv_label_create(lv_scr_act());
        lv_label_set_text(label_voltage, "0.00 V");
        lv_obj_add_style(label_voltage, &style_big_bold, 0); 
        lv_obj_align(label_voltage, LV_ALIGN_CENTER, 0, -30);

        label_current = lv_label_create(lv_scr_act());
        lv_label_set_text(label_current, "0.00 A");
        lv_obj_add_style(label_current, &style_big_bold, 0); 
        lv_obj_align(label_current, LV_ALIGN_CENTER, 0, 20);

        label_setpoint = lv_label_create(lv_scr_act());
        lv_label_set_text(label_setpoint, "Objetivo: 0.00 A");
        lv_obj_add_style(label_setpoint, &style_label_medium, 0); 
        lv_obj_set_style_text_color(label_setpoint, lv_color_hex(0x10b981), 0); 
        lv_label_set_recolor(label_setpoint, true); // Permite tags HTML de colores
        lv_obj_align(label_setpoint, LV_ALIGN_BOTTOM_MID, 0, -15);
        lv_obj_add_flag(label_setpoint, LV_OBJ_FLAG_HIDDEN); // Oculto al inicio (Estado Monitor)

        lvgl_port_unlock();
    }
}

void task_display_update(void *pvParameters) {
    display_init_hardware_and_ui();
    ui_update_t new_data;
    
    // Aumentamos un poco el buffer para permitir las etiquetas HTML de colores
    char str_buffer[64]; 

    ESP_LOGI(TAG, "Tarea gráfica OK");

    while (1) {
        if (xQueueReceive(display_queue, &new_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            if (lvgl_port_lock(pdMS_TO_TICKS(10))) {
                
                // --- ACTUALIZACIÓN DE SENSORES ---
                if (new_data.source == UI_MSG_FROM_ADC) {
                    snprintf(str_buffer, sizeof(str_buffer), "%.2f V", new_data.voltage);
                    lv_label_set_text(label_voltage, str_buffer);

                    snprintf(str_buffer, sizeof(str_buffer), "%.2f A", new_data.current);
                    lv_label_set_text(label_current, str_buffer);
                }
                
                // --- ACTUALIZACIÓN DE INTERFAZ (MONITOR VS CONFIG) ---
                else if (new_data.source == UI_MSG_FROM_SYSMAN) {
                    
                    if (new_data.ui_state == 0) {
                        // 1. ESTADO MONITOR (AZUL Y LIMPIO)
                        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x0f172a), 0); // Azul oscuro
                        lv_obj_add_flag(label_setpoint, LV_OBJ_FLAG_HIDDEN); // Ocultar objetivo
                        
                        lv_obj_set_style_text_color(label_mode, lv_color_hex(0x3b82f6), 0);
                        snprintf(str_buffer, sizeof(str_buffer), "MODO: %s", (new_data.mode == MODE_CC) ? "CC" : "CR");
                        lv_label_set_text(label_mode, str_buffer);
                    } 
                    else {
                        // 2. ESTADO CONFIGURACIÓN (OSCURO CON CURSORES)
                        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1c1917), 0); // Negro/Gris
                        lv_obj_clear_flag(label_setpoint, LV_OBJ_FLAG_HIDDEN); // Mostrar objetivo
                        
                        // Formateo del Título (Cursor pos 0 = Edición de Modo)
                        lv_obj_set_style_text_color(label_mode, lv_color_hex(0xf59e0b), 0); // Naranja base
                        if (new_data.cursor_pos == 0) {
                            snprintf(str_buffer, sizeof(str_buffer), "AJUSTE: #ff9900 %s#", (new_data.mode == MODE_CC) ? "CC" : "CR");
                        } else {
                            snprintf(str_buffer, sizeof(str_buffer), "AJUSTE: %s", (new_data.mode == MODE_CC) ? "CC" : "CR");
                        }
                        lv_label_set_text(label_mode, str_buffer);

                        // Formateo del Setpoint (Cursor pos 1, 2, 3 = Edición de dígitos)
                        if (new_data.mode == MODE_CC) {
                            int u = (int)new_data.setpoint;
                            int d = (int)(new_data.setpoint * 10) % 10;
                            int c = (int)(new_data.setpoint * 100) % 10;

                            if (new_data.cursor_pos == 1) 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: #ff9900 %d#.%d%d A", u, d, c);
                            else if (new_data.cursor_pos == 2) 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: %d.#ff9900 %d#%d A", u, d, c);
                            else if (new_data.cursor_pos == 3) 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: %d.%d#ff9900 %d# A", u, d, c);
                            else 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: %d.%d%d A", u, d, c);
                        } 
                        else { // MODE CR
                            int dec = (int)new_data.setpoint / 10;
                            int uni = (int)new_data.setpoint % 10;

                            if (new_data.cursor_pos == 1) 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: #ff9900 %d#%d Ohm", dec, uni);
                            else if (new_data.cursor_pos == 2) 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: %d#ff9900 %d# Ohm", dec, uni);
                            else 
                                snprintf(str_buffer, sizeof(str_buffer), "Obj: %d%d Ohm", dec, uni);
                        }
                        
                        lv_obj_set_style_text_color(label_setpoint, lv_color_hex(0x94a3b8), 0); // Gris claro base
                        lv_label_set_text(label_setpoint, str_buffer);
                    }
                }
                lvgl_port_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}