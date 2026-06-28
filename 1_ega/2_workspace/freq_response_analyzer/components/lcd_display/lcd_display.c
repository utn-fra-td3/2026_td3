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
#define SWCHART_DB_MIN -40    // escala fija del eje Y
#define SWCHART_DB_MAX 3     // escala fija del eje Y

// --- Variables privadas ---
static const char *TAG = "lcd_display";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_t *disp_handle = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static lv_chart_series_t *swchart_serie = NULL;
static lv_anim_t *swindicator_anim = NULL;
static uint16_t swchart_idx = 0;
static char swchart_xaxis_buf[SWCHART_XAXIS_TICKS][12];
static const char *swchart_xaxis_txt_src[SWCHART_XAXIS_TICKS + 1];

static const char *UNIT_CONFIG[] = {"Hz", "Hz", "pts", "ms"};

static lv_obj_t **val_labels_config[] = {
    &ui_lblvalue1, &ui_lblvalue2,
    &ui_lblvalue3, &ui_lblvalue4};

static const char *TEXTO_ERROR_SWEEP[] = {
    "", // SWEEP_START_OK, no se usa
    "Frecuencia inicial fuera de rango",
    "Frecuencia final fuera de rango",
    "La frecuencia inicial debe ser menor que la final",
    "Cantidad de puntos fuera de rango",
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
    esp_log_level_set("gpio", ESP_LOG_NONE); //ignorar error gpio: gpio_install_isr_service(533): GPIO isr service already installed
    lvgl_port_add_touch(&touch_cfg);
    esp_log_level_set("gpio", ESP_LOG_ERROR);

    ESP_LOGI(TAG, "Touch CST816D inicializado");
}

void task_lcd_display(void *pvParameters)
{
    lcd_init();
    lvgl_init();
    touch_init();

    configASSERT(lvgl_port_lock(portMAX_DELAY)); // tomar mutex de LVGL

    ui_init();
    ui_scrconfig_kb_init();
    swchart_serie = lv_chart_add_series(ui_swchart, lv_color_hex(0xF5D020), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_axis_range(ui_swchart, LV_CHART_AXIS_PRIMARY_Y, SWCHART_DB_MIN, SWCHART_DB_MAX);
    lv_scale_set_range(ui_swchart_Yaxis1, SWCHART_DB_MIN, SWCHART_DB_MAX);
    swindicator_anim = animindicatorpulse_Animation(ui_swindicator, 0);

    lvgl_port_unlock(); // liberar mutex de LVGL

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
                break;
            case DISPLAY_MSG_SWEEP_START_ERROR:
                mostrar_popup_error(msg.motivo);
                break;
            case DISPLAY_MSG_SWEEP_POINT:
                ESP_LOGI(TAG, "punto recibido: %lu Hz, %.2f dB", msg.freq_hz, msg.db);
                swchart_agregar_punto(msg.db);
                break;
            case DISPLAY_MSG_SHOW_SWEEP:
                lv_anim_resume(swindicator_anim);
                lv_disp_load_scr(ui_scrsweep);
                break;
            case DISPLAY_MSG_SHOW_CONFIG:
                lv_label_set_text(ui_lblbtnpausar, "PAUSAR");
                lv_label_set_text(ui_lblbtncancelar, "CANCELAR");
                lv_disp_load_scr(ui_scrconfig);
                break;
            case DISPLAY_MSG_SHOW_PAUSE:
                lv_label_set_text(ui_lblbtnpausar, "REANUDAR");
                lv_anim_pause(swindicator_anim);
                break;
            case DISPLAY_MSG_SHOW_RESUME:
                lv_label_set_text(ui_lblbtnpausar, "PAUSAR");
                lv_anim_resume(swindicator_anim);
                break;
            case DISPLAY_MSG_SHOW_CANCEL:
                lv_label_set_text(ui_lblbtncancelar, "CONFIGURAR");
                break;
            }
            lvgl_port_unlock();
        }
    }
}

static void mostrar_config_value(sweep_param_e param, uint32_t value)
{
    char tmp[20];
    bool es_frecuencia = (param == SWEEP_PARAM_FREC_INICIO || param == SWEEP_PARAM_FREC_FINAL);
    if (es_frecuencia && value >= 1000)
    {
        uint32_t khz_entero = value / 1000;
        uint32_t khz_resto = value % 1000;
        if (khz_resto == 0)
        {
            snprintf(tmp, sizeof(tmp), "%lu kHz", khz_entero);
        }
        else if (khz_resto % 100 == 0)
        {
            snprintf(tmp, sizeof(tmp), "%lu,%lu kHz", khz_entero, khz_resto / 100);
        }
        else if (khz_resto % 10 == 0)
        {
            snprintf(tmp, sizeof(tmp), "%lu,%02lu kHz", khz_entero, khz_resto / 10);
        }
        else
        {
            snprintf(tmp, sizeof(tmp), "%lu,%03lu kHz", khz_entero, khz_resto);
        }
    }
    else
    {
        snprintf(tmp, sizeof(tmp), "%lu %s", value, UNIT_CONFIG[param]);
    }
    lv_label_set_text(*val_labels_config[param], tmp);
}

static void mostrar_popup_error(sweep_start_result_e motivo)
{
    lv_label_set_text(ui_uicfgpopuplbl, TEXTO_ERROR_SWEEP[motivo]);
    lv_obj_remove_flag(ui_uicfgpopup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui_uicfgpopup);
}

// mismo paso logaritmico que en task_sweep
static void swchart_actualizar_escala_frecuencia(uint32_t frec_inicio, uint32_t frec_final)
{
    for (int i = 0; i < SWCHART_XAXIS_TICKS; i++)
    {
        uint32_t frec;
        if (i >= SWCHART_XAXIS_TICKS - 1)
        {
            frec = frec_final;
        }
        else
        {
            float exponente = (float)i / (float)(SWCHART_XAXIS_TICKS - 1);
            frec = (uint32_t)(frec_inicio * powf((float)frec_final / (float)frec_inicio, exponente) + 0.5f);
        }

        if (frec >= 1000)
        {
            uint32_t khz_entero = frec / 1000;
            uint32_t khz_decimal = ((frec % 1000) + 50) / 100;
            if (khz_decimal >= 10)
            {
                khz_decimal = 0;
                khz_entero++;
            }
            if (khz_decimal == 0)
            {
                snprintf(swchart_xaxis_buf[i], sizeof(swchart_xaxis_buf[i]), "%luk", khz_entero);
            }
            else
            {
                snprintf(swchart_xaxis_buf[i], sizeof(swchart_xaxis_buf[i]), "%lu,%luk", khz_entero, khz_decimal);
            }
        }
        else
        {
            snprintf(swchart_xaxis_buf[i], sizeof(swchart_xaxis_buf[i]), "%lu", frec);
        }

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
}

static void swchart_agregar_punto(float db)
{
    if (swchart_idx >= lv_chart_get_point_count(ui_swchart))
    {
        ESP_LOGW(TAG, "chart lleno, punto no graficado");
        return;
    }

    int32_t valor = lroundf(db);
    if (valor < SWCHART_DB_MIN)
    {
        valor = SWCHART_DB_MIN;
    }
    if (valor > SWCHART_DB_MAX)
    {
        valor = SWCHART_DB_MAX;
    }

    lv_chart_set_value_by_id(ui_swchart, swchart_serie, swchart_idx, valor);
    swchart_idx++;
}
