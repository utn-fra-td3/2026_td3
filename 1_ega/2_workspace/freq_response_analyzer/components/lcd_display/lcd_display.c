// --- Includes ---
#include "lcd_display.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "ui.h"
#include "ui_scrconfig_kb.h"
#include <stdbool.h>
#include <stdio.h>
#include <math.h>

// --- Defines privados ---
#define LCD_SCLK GPIO_NUM_39
#define LCD_MOSI GPIO_NUM_38
#define LCD_CS GPIO_NUM_45
#define LCD_DC GPIO_NUM_42
#define LCD_RST GPIO_NUM_0
#define LCD_BL GPIO_NUM_1

#define LCD_H_RES 320
#define LCD_V_RES 240

#define TOUCH_SDA GPIO_NUM_48
#define TOUCH_SCL GPIO_NUM_47
#define TOUCH_INT GPIO_NUM_46

#define SWCHART_XAXIS_TICKS 5 // cantidad de ticks mayores del eje X
#define SWCHART_DB_PISO -80 // minimo real medible, el eje Y nunca se expande mas abajo de esto

// --- Variables privadas ---
static const char *TAG = "lcd_display";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_t *disp_handle = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static lv_chart_series_t *swchart_serie = NULL;
static uint16_t swchart_idx = 0;
static int32_t swchart_db_min = 0;
static int32_t swchart_db_max = 0;
static bool swchart_tiene_datos = false;
static char swchart_xaxis_buf[SWCHART_XAXIS_TICKS][8];
static const char *swchart_xaxis_txt_src[SWCHART_XAXIS_TICKS + 1];

static const char *UNIT_CONFIG[] = {"Hz", "Hz", "pts", "ms"};

static lv_obj_t **val_labels_config[] = {
    &ui_lblvalue1, &ui_lblvalue2,
    &ui_lblvalue3, &ui_lblvalue4};

static const char *TEXTO_ERROR_SWEEP[] = {
    "", // SWEEP_START_OK, no se usa
    "Frecuencia inicial fuera de rango (10 - 99999 Hz)",
    "Frecuencia final fuera de rango (11 - 100000 Hz)",
    "La frecuencia inicial debe ser menor que la final",
    "Cantidad de puntos fuera de rango (2 - 512)",
    "Tiempo de asentamiento insuficiente para la frecuencia inicial",
};

// --- Prototipos privados ---
static void lcd_init(void);
static void lvgl_init(void);
static void touch_init(void);
static void mostrar_config_value(sweep_param_e param, uint32_t value);
static void mostrar_popup_error(sweep_start_result_e motivo);
static void swchart_actualizar_escala_frecuencia(uint32_t frec_inicio, uint32_t frec_final);
static void swchart_reiniciar(uint32_t puntos);
static void swchart_agregar_punto(float db);

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
            .mirror_x = false,
            .mirror_y = true,
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

static void touch_init(void)
{
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_V_RES,
        .y_max = LCD_H_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &tp_handle));

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp_handle,
        .handle = tp_handle,
    };
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "Touch CST816D inicializado");
}

void task_lcd_display(void *pvParameters)
{
    lcd_init();
    lvgl_init();
    touch_init();

    configASSERT(lvgl_port_lock(portMAX_DELAY));
    ui_init();
    ui_scrconfig_kb_init();
    swchart_serie = lv_chart_add_series(ui_swchart, lv_color_hex(0xF5D020), LV_CHART_AXIS_PRIMARY_Y);
    animindicatorpulse_Animation(ui_swindicator, 0);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "UI inicializada");

    display_msg_t msg;
    while (1)
    {
        if (xQueueReceive(queue_display, &msg, portMAX_DELAY) == pdTRUE)
        {
            configASSERT(lvgl_port_lock(portMAX_DELAY));
            switch (msg.type)
            {
            case DISPLAY_MSG_CONFIG_VALUE:
                mostrar_config_value(msg.param, msg.value);
                break;
            case DISPLAY_MSG_SWEEP_START_OK:
                swchart_actualizar_escala_frecuencia(msg.frec_inicio, msg.frec_final);
                swchart_reiniciar(msg.puntos);
                lv_disp_load_scr(ui_scrsweep);
                break;
            case DISPLAY_MSG_SWEEP_START_ERROR:
                mostrar_popup_error(msg.motivo);
                break;
            case DISPLAY_MSG_SWEEP_POINT:
                ESP_LOGI(TAG, "punto recibido: %lu Hz, %.2f dB", msg.freq_hz, msg.db);
                swchart_agregar_punto(msg.db);
                break;
            }
            lvgl_port_unlock();
        }
    }
}

static void mostrar_config_value(sweep_param_e param, uint32_t value)
{
    char tmp[16];
    bool es_frecuencia = (param == SWEEP_PARAM_FREC_INICIO || param == SWEEP_PARAM_FREC_FINAL);
    if (es_frecuencia && value >= 1000)
    {
        uint32_t khz_entero = value / 1000;
        uint32_t khz_decimal = (value % 1000) / 100;
        if (khz_decimal == 0)
            snprintf(tmp, sizeof(tmp), "%lu kHz", khz_entero);
        else
            snprintf(tmp, sizeof(tmp), "%lu.%lu kHz", khz_entero, khz_decimal);
    }
    else
        snprintf(tmp, sizeof(tmp), "%lu %s", value, UNIT_CONFIG[param]);
    lv_label_set_text(*val_labels_config[param], tmp);
}

static void mostrar_popup_error(sweep_start_result_e motivo)
{
    lv_label_set_text(ui_uicfgpopuplbl, TEXTO_ERROR_SWEEP[motivo]);
    lv_obj_remove_flag(ui_uicfgpopup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_uicfgpopup);
}

// paso logaritmico, igual al usado por task_sweep: f(i) = frec_inicio * (frec_final/frec_inicio)^(i/(ticks-1))
static void swchart_actualizar_escala_frecuencia(uint32_t frec_inicio, uint32_t frec_final)
{
    for (int i = 0; i < SWCHART_XAXIS_TICKS; i++)
    {
        double exponente = (double)i / (double)(SWCHART_XAXIS_TICKS - 1);
        double frec = frec_inicio * pow((double)frec_final / (double)frec_inicio, exponente);

        if (frec >= 1000.0)
            snprintf(swchart_xaxis_buf[i], sizeof(swchart_xaxis_buf[i]), "%.1fk", frec / 1000.0);
        else
            snprintf(swchart_xaxis_buf[i], sizeof(swchart_xaxis_buf[i]), "%lu", (uint32_t)(frec + 0.5));

        swchart_xaxis_txt_src[i] = swchart_xaxis_buf[i];
    }
    swchart_xaxis_txt_src[SWCHART_XAXIS_TICKS] = NULL;

    lv_scale_set_text_src(ui_swchart_Xaxis, swchart_xaxis_txt_src);
}

static void swchart_reiniciar(uint32_t puntos)
{
    lv_chart_set_point_count(ui_swchart, puntos);
    lv_chart_set_all_value(ui_swchart, swchart_serie, LV_CHART_POINT_NONE);
    swchart_idx = 0;
    swchart_tiene_datos = false;
}

static void swchart_agregar_punto(float db)
{
    if (swchart_idx >= lv_chart_get_point_count(ui_swchart))
    {
        ESP_LOGW(TAG, "chart lleno, punto no graficado");
        return;
    }

    int32_t valor = LV_MAX(lroundf(db), SWCHART_DB_PISO);

    if (!swchart_tiene_datos)
    {
        swchart_db_min = valor - 1;
        swchart_db_max = valor + 1;
        swchart_tiene_datos = true;
        lv_chart_set_axis_range(ui_swchart, LV_CHART_AXIS_PRIMARY_Y, swchart_db_min, swchart_db_max);
        lv_scale_set_range(ui_swchart_Yaxis1, swchart_db_min, swchart_db_max);
    }
    else if (valor < swchart_db_min || valor > swchart_db_max)
    {
        swchart_db_min = LV_MAX(LV_MIN(swchart_db_min, valor), SWCHART_DB_PISO);
        swchart_db_max = LV_MAX(swchart_db_max, valor);
        lv_chart_set_axis_range(ui_swchart, LV_CHART_AXIS_PRIMARY_Y, swchart_db_min, swchart_db_max);
        lv_scale_set_range(ui_swchart_Yaxis1, swchart_db_min, swchart_db_max);
    }

    lv_chart_set_value_by_id(ui_swchart, swchart_serie, swchart_idx, valor);
    swchart_idx++;
}
