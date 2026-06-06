// --- Includes ---
#include "lcd_display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

// --- Defines privados ---
#define LCD_SCLK GPIO_NUM_39
#define LCD_MOSI GPIO_NUM_38
#define LCD_CS GPIO_NUM_45
#define LCD_DC GPIO_NUM_42
#define LCD_RST GPIO_NUM_0
#define LCD_BL GPIO_NUM_1

#define LCD_H_RES 320
#define LCD_V_RES 240

// --- Variables privadas ---
static const char *TAG = "lcd_display";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_t *disp_handle = NULL;

// --- Prototipos privados ---
static void lcd_init(void);
static void lvgl_init(void);

// --- Funciones ---

static void lcd_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_cfg, &io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 1);

    ESP_LOGI(TAG, "ST7789 inicializado");
}

static void lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * 50,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,
        },
    };
    disp_handle = lvgl_port_add_disp(&disp_cfg);
    configASSERT(disp_handle != NULL);

    ESP_LOGI(TAG, "LVGL inicializado");
}

void task_lcd_display(void *pvParameters)
{
    lcd_init();
    lvgl_init();

    if (lvgl_port_lock(0))
    {
        lv_obj_t *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xFF0000), LV_PART_MAIN);
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text(label, "Prueba de Display");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Display listo");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
