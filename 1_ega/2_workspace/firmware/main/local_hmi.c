#include "local_hmi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad9834_driver.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"
#include "uart_console.h"

#define HMI_WIDTH 240
#define HMI_HEIGHT 320
#define HMI_BUFFER_SIZE (HMI_WIDTH * 20 * sizeof(uint16_t))
#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPPUA 0x0C
#define MCP_GPIOA 0x12
#define MCP_OLATB 0x15

static const char *TAG = "LOCAL_HMI";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_keypad;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_display;
static void *s_pixels;
static bool s_spi_ready;
static lv_obj_t *s_home;
static lv_obj_t *s_plot;
static lv_obj_t *s_status_label;
static lv_obj_t *s_field_labels[4];
static lv_obj_t *s_field_values[4];
static lv_obj_t *s_entry_label;
static lv_obj_t *s_result_label;
static lv_obj_t *s_plot_heading;
static lv_obj_t *s_plot_status;
static lv_obj_t *s_plot_readout;
static lv_obj_t *s_frequency_labels[3];
static lv_obj_t *s_axis_titles[2];
static lv_obj_t *s_axis_labels[2][3];
static lv_obj_t *s_plot_views[2];
static lv_obj_t *s_charts[2];
static lv_chart_series_t *s_series[2];
static bool s_plot_page;
static bool s_phase_plot;
static bool s_running;
static uint32_t s_received;
static uint32_t s_range_warnings;
static bool s_raw_plot;
static ad8302_sample_t s_plot_sample;
static unsigned s_field;
static uint32_t s_values[4];
static bool s_simulated = false;
static char s_entry[11];
static bool s_entry_overflow;
static uint32_t s_completion;
static uint16_t s_candidate;
static uint16_t s_stable;
static unsigned s_debounce;
static unsigned s_i2c_failures;
static QueueHandle_t s_key_events;
static TaskHandle_t s_keypad_task;
static char s_message[64] = "READY";

static esp_err_t mcp_write(uint8_t reg, uint8_t value)
{
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(s_keypad, bytes, sizeof(bytes), 20);
}

static esp_err_t keypad_configure(void)
{
    esp_err_t ret = mcp_write(MCP_IODIRA, 0xFF);
    if (ret == ESP_OK) ret = mcp_write(MCP_IODIRB, 0xFF);
    if (ret == ESP_OK) ret = mcp_write(MCP_GPPUA, 0x0F);
    if (ret == ESP_OK) ret = mcp_write(MCP_OLATB, 0x00);
    return ret;
}

static esp_err_t keypad_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_22,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t ret = i2c_new_master_bus(&bus, &s_i2c_bus);
    if (ret != ESP_OK) return ret;
    i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
#ifdef CONFIG_EGA_HMI_I2C_ADDRESS
        .device_address = CONFIG_EGA_HMI_I2C_ADDRESS,
#else
        .device_address = 0x27,
#endif
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &device, &s_keypad);
    if (ret == ESP_OK) ret = keypad_configure();
    return ret;
}

static esp_err_t keypad_scan(uint16_t *keys)
{
    *keys = 0;
    for (unsigned row = 0; row < 4; row++) {
        esp_err_t ret = mcp_write(MCP_IODIRB, (uint8_t)(0xFFU & ~(1U << row)));
        if (ret != ESP_OK) return ret;
        uint8_t reg = MCP_GPIOA;
        uint8_t inputs = 0xFF;
        ret = i2c_master_transmit_receive(s_keypad, &reg, 1, &inputs, 1, 20);
        esp_err_t release = mcp_write(MCP_IODIRB, 0xFF);
        if (ret != ESP_OK) return ret;
        if (release != ESP_OK) return release;
        *keys |= (uint16_t)((~inputs & 0x0FU) << (row * 4));
    }
    return ESP_OK;
}

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context)
{
    (void)io;
    (void)event;
    lv_display_flush_ready(context);
    return false;
}

static void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    size_t count = (size_t)lv_area_get_width(area) * lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(pixels, count);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                                             area->x2 + 1, area->y2 + 1, pixels);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display transfer: %s", esp_err_to_name(ret));
        lv_display_flush_ready(display);
    }
}

static esp_err_t display_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = GPIO_NUM_13,
        .miso_io_num = -1,
        .sclk_io_num = GPIO_NUM_14,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = HMI_BUFFER_SIZE,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;
    s_spi_ready = true;
    esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = GPIO_NUM_27,
        .dc_gpio_num = GPIO_NUM_32,
        .spi_mode = 0,
        .pclk_hz = 10000000,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s_panel_io);
    esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = GPIO_NUM_33,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    if (ret == ESP_OK) ret = esp_lcd_new_panel_ili9341(s_panel_io, &config, &s_panel);
    if (ret == ESP_OK) ret = esp_lcd_panel_reset(s_panel);
    if (ret == ESP_OK) ret = esp_lcd_panel_init(s_panel);
    if (ret == ESP_OK) ret = esp_lcd_panel_mirror(s_panel, true, false);
    if (ret == ESP_OK) ret = esp_lcd_panel_disp_on_off(s_panel, true);
    if (ret != ESP_OK) return ret;
    s_pixels = heap_caps_malloc(HMI_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_pixels == NULL) return ESP_ERR_NO_MEM;
    lv_init();
    s_display = lv_display_create(HMI_WIDTH, HMI_HEIGHT);
    if (s_display == NULL) return ESP_ERR_NO_MEM;
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_pixels, NULL, HMI_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, display_flush);
    esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = color_done};
    return esp_lcd_panel_io_register_event_callbacks(s_panel_io, &callbacks, s_display);
}

static lv_obj_t *make_label(lv_obj_t *parent, int left, int top, int width, int height)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, left, top);
    lv_obj_set_size(label, width, height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static lv_obj_t *make_page(void)
{
    lv_obj_t *page = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, HMI_WIDTH, HMI_HEIGHT);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void create_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEDF1F2), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x243238), 0);
    lv_obj_set_style_text_letter_space(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_home = make_page();
    lv_obj_t *title = make_label(s_home, 0, 0, HMI_WIDTH, 32);
    lv_obj_set_style_bg_color(title, lv_color_hex(0x126B62), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_left(title, 10, 0);
    lv_obj_set_style_pad_top(title, 8, 0);
    lv_label_set_text(title, "EGA  /  FREQUENCY RESPONSE");
    s_status_label = make_label(s_home, 10, 36, 220, 34);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x596C73), 0);
    static const char *field_names[4] = {"Start Hz", "Stop Hz", "Points", "Total ms"};
    for (unsigned field = 0; field < 4; field++) {
        s_field_labels[field] = make_label(s_home, 8, 72 + field * 30, 96, 28);
        s_field_values[field] = make_label(s_home, 104, 72 + field * 30, 128, 28);
        lv_label_set_text(s_field_labels[field], field_names[field]);
        lv_obj_set_style_pad_left(s_field_labels[field], 6, 0);
        lv_obj_set_style_pad_top(s_field_labels[field], 6, 0);
        lv_obj_set_style_pad_top(s_field_values[field], 6, 0);
        lv_obj_set_style_pad_right(s_field_values[field], 6, 0);
        lv_obj_set_style_text_align(s_field_values[field], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_bg_opa(s_field_labels[field], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(s_field_values[field], LV_OPA_COVER, 0);
    }
    s_entry_label = make_label(s_home, 8, 194, 224, 28);
    lv_obj_set_style_bg_color(s_entry_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_entry_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(s_entry_label, 6, 0);
    lv_obj_set_style_pad_top(s_entry_label, 6, 0);
    s_result_label = make_label(s_home, 10, 226, 220, 34);
    static const char *key_actions[6] = {
        "B  Next field", "C  SIM / ADC", "*  Delete", "#  Accept", "A  Stop / Home", "D  Start"
    };
    for (unsigned action = 0; action < 6; action++) {
        lv_obj_t *legend = make_label(s_home, 8 + (action % 2) * 116,
                                      264 + (action / 2) * 18, 108, 18);
        lv_obj_set_style_text_color(legend, lv_color_hex(0x596C73), 0);
        lv_label_set_text(legend, key_actions[action]);
    }
    s_plot = make_page();
    s_plot_heading = make_label(s_plot, 8, 4, 224, 20);
    s_plot_status = make_label(s_plot, 8, 26, 224, 20);
    lv_obj_set_style_text_color(s_plot_heading, lv_color_hex(0x126B62), 0);
    static const char *axis_labels[2][3] = {{"+30", "0", "-30"}, {"180", "90", "0"}};
    for (unsigned chart = 0; chart < 2; chart++) {
        s_plot_views[chart] = lv_obj_create(s_plot);
        lv_obj_remove_style_all(s_plot_views[chart]);
        lv_obj_set_pos(s_plot_views[chart], 0, 48);
        lv_obj_set_size(s_plot_views[chart], HMI_WIDTH, 184);
        lv_obj_remove_flag(s_plot_views[chart], LV_OBJ_FLAG_SCROLLABLE);
        s_axis_titles[chart] = make_label(s_plot_views[chart], 8, 0, 224, 18);
        lv_label_set_text(s_axis_titles[chart],
                          chart == 0 ? "MAG dB [-30, +30]" : "PHS |deg| [0, 180]");
        s_charts[chart] = lv_chart_create(s_plot_views[chart]);
        lv_obj_set_pos(s_charts[chart], 42, 20);
        lv_obj_set_size(s_charts[chart], 190, 160);
        lv_obj_set_style_pad_all(s_charts[chart], 0, 0);
        lv_obj_set_style_radius(s_charts[chart], 0, 0);
        lv_obj_set_style_bg_color(s_charts[chart], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_charts[chart], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_charts[chart], 1, 0);
        lv_obj_set_style_border_color(s_charts[chart], lv_color_hex(0xC7D2D6), 0);
        lv_obj_set_style_line_color(s_charts[chart], lv_color_hex(0xE2E9EB), LV_PART_MAIN);
        lv_obj_set_style_line_width(s_charts[chart], 2, LV_PART_ITEMS);
        lv_obj_set_style_size(s_charts[chart], 3, 3, LV_PART_INDICATOR);
        lv_chart_set_type(s_charts[chart], LV_CHART_TYPE_SCATTER);
        lv_chart_set_point_count(s_charts[chart], ANALYZER_LOCAL_MAX_POINTS);
        lv_chart_set_range(s_charts[chart], LV_CHART_AXIS_PRIMARY_X, 0, 1000);
        lv_chart_set_range(s_charts[chart], LV_CHART_AXIS_PRIMARY_Y,
                           chart == 0 ? -300 : 0, chart == 0 ? 300 : 1800);
        lv_chart_set_div_line_count(s_charts[chart], 3, 3);
        s_series[chart] = lv_chart_add_series(s_charts[chart],
                              lv_color_hex(chart == 0 ? 0x126B62 : 0xC15B39), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_set_style_text_color(s_axis_titles[chart], lv_color_hex(chart == 0 ? 0x126B62 : 0xC15B39), 0);
        for (unsigned tick = 0; tick < 3; tick++) {
            s_axis_labels[chart][tick] = make_label(s_plot_views[chart], 4, 20 + tick * 71, 36, 18);
            lv_label_set_text(s_axis_labels[chart][tick], axis_labels[chart][tick]);
            lv_obj_set_style_text_color(s_axis_labels[chart][tick], lv_color_hex(0x596C73), 0);
        }
        if (chart != 0) lv_obj_add_flag(s_plot_views[chart], LV_OBJ_FLAG_HIDDEN);
    }
    for (unsigned tick = 0; tick < 3; tick++) {
        s_frequency_labels[tick] = make_label(s_plot, 8 + tick * 76, 236, 72, 18);
        lv_obj_set_style_text_color(s_frequency_labels[tick], lv_color_hex(0x596C73), 0);
        lv_obj_set_style_text_align(s_frequency_labels[tick],
                                   tick == 0 ? LV_TEXT_ALIGN_LEFT : (tick == 1 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT), 0);
    }
    s_plot_readout = make_label(s_plot, 8, 256, 224, 36);
    lv_obj_t *plot_legend = make_label(s_plot, 8, 298, 112, 18);
    lv_obj_set_style_text_color(plot_legend, lv_color_hex(0x596C73), 0);
    lv_label_set_text(plot_legend, "A  Stop / Home");
    plot_legend = make_label(s_plot, 132, 298, 100, 18);
    lv_obj_set_style_text_color(plot_legend, lv_color_hex(0x596C73), 0);
    lv_label_set_text(plot_legend, "B  MAG / PHS");
    lv_obj_add_flag(s_plot, LV_OBJ_FLAG_HIDDEN);
}

static void set_plot_units(bool raw)
{
    static const char *nominal_labels[2][3] = {{"+30", "0", "-30"}, {"180", "90", "0"}};
    static const char *raw_labels[3] = {"4095", "2048", "0"};
    s_raw_plot = raw;
    for (unsigned chart = 0; chart < 2; chart++) {
        lv_label_set_text(s_axis_titles[chart], raw ?
                          (chart == 0 ? "MAG ADC raw (counts)" : "PHS ADC raw (counts)") :
                          (chart == 0 ? "MAG dB [-30, +30]" : "PHS |deg| [0, 180]"));
        lv_chart_set_range(s_charts[chart], LV_CHART_AXIS_PRIMARY_Y,
                           raw || chart != 0 ? 0 : -300,
                           raw ? 4095 : (chart == 0 ? 300 : 1800));
        for (unsigned tick = 0; tick < 3; tick++) {
            lv_label_set_text(s_axis_labels[chart][tick], raw ? raw_labels[tick] : nominal_labels[chart][tick]);
        }
    }
}

static void load_fields(const analyzer_status_t *status)
{
    s_entry[0] = '\0';
    s_entry_overflow = false;
    s_field = 0;
    s_values[0] = status->sweep.start_hz;
    s_values[1] = status->sweep.stop_hz;
    uint32_t count = status->sweep.point_count;
    if (count == 0 && status->sweep.step_hz != 0) {
        count = (status->sweep.stop_hz - status->sweep.start_hz) / status->sweep.step_hz + 1;
    }
    s_values[2] = count == 0 ? 100 : count;
    s_values[3] = status->sweep.total_time_ms;
    if (s_values[3] == 0) {
        uint32_t dwell = status->sweep.dwell_ms < 10 ? 10 : status->sweep.dwell_ms;
        uint64_t duration = (uint64_t)s_values[2] * dwell;
        s_values[3] = duration > SWEEP_MAX_TOTAL_TIME_MS ? SWEEP_MAX_TOTAL_TIME_MS : (uint32_t)duration;
    }
    s_simulated = status->sweep.measurement_source == AD8302_SOURCE_SIM;
}

static bool publish_settings(const uint32_t values[4], bool simulated)
{
    sweep_config_t config = {
        .start_hz = values[0], .stop_hz = values[1],
        .point_count = values[2], .total_time_ms = values[3],
        .adc_channel = AD8302_MAG_CHANNEL,
        .measurement_source = simulated ? AD8302_SOURCE_SIM : AD8302_SOURCE_ADC,
    };
    if (sweep_validate(&config) != ESP_OK) {
        snprintf(s_message, sizeof(s_message), "ERR: range, points or time");
        return false;
    }
    char command[96];
    snprintf(command, sizeof(command), "SWEEP_CONFIG %s %lu %lu %lu %lu",
             simulated ? "SIM" : "ADC", (unsigned long)values[0],
             (unsigned long)values[1], (unsigned long)values[2], (unsigned long)values[3]);
    esp_err_t ret = analyzer_submit_command(command);
    if (ret != ESP_OK) {
        snprintf(s_message, sizeof(s_message), "%s", ret == ESP_ERR_INVALID_STATE ?
                 "BUSY: retry setting" : esp_err_to_name(ret));
        return false;
    }
    return true;
}

static bool commit_entry(bool publish)
{
    if (s_entry_overflow) {
        snprintf(s_message, sizeof(s_message), "ERR: entry exceeds 10 digits");
        return false;
    }
    if (s_entry[0] == '\0') return true;
    unsigned long long value = strtoull(s_entry, NULL, 10);
    if (value > UINT32_MAX) {
        snprintf(s_message, sizeof(s_message), "ERR: value out of range");
        return false;
    }
    if (s_field < 2 && (value < AD9834_MIN_FREQUENCY_HZ ||
                        value > ad9834_get_max_frequency_hz())) {
        snprintf(s_message, sizeof(s_message), "ERR: frequency 100k..37.5M Hz");
        return false;
    }
    if (s_field == 2) {
        if (value == 0 || value > ANALYZER_LOCAL_MAX_POINTS) {
            snprintf(s_message, sizeof(s_message), "ERR: points 1..%u", (unsigned)ANALYZER_LOCAL_MAX_POINTS);
            return false;
        }
        if (s_values[0] > s_values[1] || value > (uint64_t)s_values[1] - s_values[0] + 1U ||
            (value == 1 && s_values[0] != s_values[1])) {
            snprintf(s_message, sizeof(s_message), "ERR: points do not fit Hz range");
            return false;
        }
    }
    if (s_field == 2 || s_field == 3) {
        uint32_t points = s_field == 2 ? (uint32_t)value : s_values[2];
        uint32_t duration = s_field == 3 ? (uint32_t)value : s_values[3];
        uint32_t minimum_ms = (uint32_t)(((uint64_t)points * 1000U + configTICK_RATE_HZ - 1U) /
                                         configTICK_RATE_HZ);
        if (duration < minimum_ms || duration > SWEEP_MAX_TOTAL_TIME_MS) {
            if (s_field == 2 && duration < minimum_ms) {
                snprintf(s_message, sizeof(s_message), "ERR: increase time first\nMinimum: %lu ms", (unsigned long)minimum_ms);
            } else {
                snprintf(s_message, sizeof(s_message), "ERR: time out of range\n%lu..%lu ms",
                         (unsigned long)minimum_ms, (unsigned long)SWEEP_MAX_TOTAL_TIME_MS);
            }
            return false;
        }
    }
    uint32_t proposed[4];
    memcpy(proposed, s_values, sizeof(proposed));
    proposed[s_field] = (uint32_t)value;
    if (publish && !publish_settings(proposed, s_simulated)) return false;
    s_values[s_field] = (uint32_t)value;
    s_entry[0] = '\0';
    snprintf(s_message, sizeof(s_message), "%s", publish ? "SAVING SETTINGS" : "SETTINGS UPDATED");
    return true;
}

static void show_home(void)
{
    if (s_running && analyzer_cancel_local_sweep()) {
        snprintf(s_message, sizeof(s_message), "STOPPING");
    }
    s_plot_page = false;
    s_entry[0] = '\0';
    s_entry_overflow = false;
    lv_obj_add_flag(s_plot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_home, LV_OBJ_FLAG_HIDDEN);
}

static void show_plot_metric(bool phase)
{
    s_phase_plot = phase;
    lv_obj_add_flag(s_plot_views[phase ? 0 : 1], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_plot_views[phase ? 1 : 0], LV_OBJ_FLAG_HIDDEN);
    lv_chart_refresh(s_charts[phase ? 1 : 0]);
}

static void handle_key(char key, const analyzer_status_t *status)
{
    (void)status;
    if (key == 'A') {
        show_home();
        return;
    }
    if (s_plot_page) {
        if (key == 'B') show_plot_metric(!s_phase_plot);
        return;
    }
    if (key >= '0' && key <= '9') {
        size_t length = strlen(s_entry);
        if (length < sizeof(s_entry) - 1) {
            s_entry[length] = key;
            s_entry[length + 1] = '\0';
        } else {
            s_entry_overflow = true;
            snprintf(s_message, sizeof(s_message), "ERR: entry exceeds 10 digits");
        }
        return;
    }
    if (key == '*') {
        size_t length = strlen(s_entry);
        if (length != 0) s_entry[length - 1] = '\0';
        s_entry_overflow = false;
        return;
    }
    if (key == '#') {
        commit_entry(true);
        return;
    }
    if (key == 'B') {
        if (commit_entry(true)) s_field = (s_field + 1) % 4;
        return;
    }
    if (key == 'C') {
        if (publish_settings(s_values, !s_simulated)) {
            s_simulated = !s_simulated;
            snprintf(s_message, sizeof(s_message), "SAVING SETTINGS");
        }
        return;
    }
    if (key == 'D' && commit_entry(false)) {
        if (s_values[2] > ANALYZER_LOCAL_MAX_POINTS) {
            snprintf(s_message, sizeof(s_message), "ERR: local points 1..%lu",
                     (unsigned long)ANALYZER_LOCAL_MAX_POINTS);
            return;
        }
        sweep_config_t config = {
            .start_hz = s_values[0], .stop_hz = s_values[1],
            .point_count = s_values[2], .total_time_ms = s_values[3],
            .adc_channel = AD8302_MAG_CHANNEL,
            .measurement_source = s_simulated ? AD8302_SOURCE_SIM : AD8302_SOURCE_ADC,
        };
        esp_err_t ret = analyzer_start_local_sweep(&config);
        if (ret != ESP_OK) {
            snprintf(s_message, sizeof(s_message), "%s", ret == ESP_ERR_INVALID_ARG ?
                     "ERR: range, points or time" : (ret == ESP_ERR_INVALID_STATE ? "BUSY" : esp_err_to_name(ret)));
            return;
        }
        s_received = 0;
        s_range_warnings = 0;
        set_plot_units(false);
        s_running = true;
        s_plot_page = true;
        show_plot_metric(false);
        snprintf(s_message, sizeof(s_message), "RUNNING");
        for (unsigned chart = 0; chart < 2; chart++) {
            lv_chart_set_all_value(s_charts[chart], s_series[chart], LV_CHART_POINT_NONE);
        }
        uint32_t middle = (uint32_t)sqrt((double)s_values[0] * s_values[1]);
        uint32_t ticks[] = {s_values[0], middle, s_values[1]};
        for (unsigned tick = 0; tick < 3; tick++) {
            char frequency[16];
            if (ticks[tick] >= 1000000) snprintf(frequency, sizeof(frequency), "%.2gM", ticks[tick] / 1000000.0);
            else if (ticks[tick] >= 1000) snprintf(frequency, sizeof(frequency), "%.2gk", ticks[tick] / 1000.0);
            else snprintf(frequency, sizeof(frequency), "%lu", (unsigned long)ticks[tick]);
            lv_label_set_text(s_frequency_labels[tick], frequency);
        }
        lv_obj_add_flag(s_home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_plot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_plot(void)
{
    uint32_t before = s_received;
    while (s_received < s_values[2] && analyzer_get_local_sample(s_received, &s_plot_sample)) {
        if (!s_raw_plot && (s_plot_sample.flags & AD8302_FLAG_ADC_UNCALIBRATED)) {
            set_plot_units(true);
            s_received = 0;
            s_range_warnings = 0;
            for (unsigned chart = 0; chart < 2; chart++) {
                lv_chart_set_all_value(s_charts[chart], s_series[chart], LV_CHART_POINT_NONE);
            }
            continue;
        }
        if (s_plot_sample.flags & AD8302_FLAG_RANGE_SUSPECT) s_range_warnings++;
        if (s_raw_plot || (s_plot_sample.mag_mv >= 0 && s_plot_sample.phase_mv >= 0)) {
            double span = log10((double)s_values[1] / s_values[0]);
            int32_t position = span > 0 ? (int32_t)lround(1000 * log10((double)s_plot_sample.frequency_hz / s_values[0]) / span) : 500;
            lv_chart_get_x_array(s_charts[0], s_series[0])[s_received] = position;
            lv_chart_get_x_array(s_charts[1], s_series[1])[s_received] = position;
            lv_chart_get_y_array(s_charts[0], s_series[0])[s_received] = s_raw_plot ?
                s_plot_sample.mag_raw : (s_plot_sample.mag_mv - 900) / 3;
            int32_t phase = 1800 - s_plot_sample.phase_mv;
            lv_chart_get_y_array(s_charts[1], s_series[1])[s_received] = s_raw_plot ?
                s_plot_sample.phase_raw : (phase < 0 ? 0 : (phase > 1800 ? 1800 : phase));
        }
        s_received++;
    }
    if (s_received != before) {
        for (unsigned chart = 0; chart < 2; chart++) lv_chart_refresh(s_charts[chart]);
    }
    lv_label_set_text_fmt(s_plot_heading, "%s / %s / log Hz", s_phase_plot ? "PHS" : "MAG",
                          s_simulated ? "SIM" : (s_raw_plot ? "ADC RAW" : "ADC NOM"));
    lv_label_set_text_fmt(s_plot_status, "%s  %lu/%lu", s_running ? "RUN" :
                           (strncmp(s_message, "ERR", 3) == 0 ? "ERROR" : "DONE"),
                           (unsigned long)s_received, (unsigned long)s_values[2]);
    if (s_range_warnings) {
        lv_label_set_text_fmt(s_plot_status, "%s %lu/%lu RANGE! %lu", s_running ? "RUN" :
                             (strncmp(s_message, "ERR", 3) == 0 ? "ERROR" : "DONE"),
                             (unsigned long)s_received, (unsigned long)s_values[2],
                             (unsigned long)s_range_warnings);
    }
        bool fault = s_i2c_failures || strncmp(s_message, "ERR", 3) == 0;
        lv_obj_set_style_text_color(s_plot_status, lv_color_hex(fault ? 0xB43E3B :
                                                             (s_range_warnings || s_raw_plot ? 0x966514 : 0x126B62)), 0);
        lv_obj_set_style_text_color(s_plot_readout, lv_color_hex(fault ? 0xB43E3B :
                                                             (s_raw_plot ? 0x966514 : (s_phase_plot ? 0xC15B39 : 0x126B62))), 0);
    if (s_i2c_failures || strncmp(s_message, "ERR", 3) == 0) {
        lv_label_set_text(s_plot_readout, s_i2c_failures ? "FAULT: keypad I2C" : s_message);
    } else if (s_received == 0) {
        lv_label_set_text(s_plot_readout, "Waiting for samples");
    } else if (s_raw_plot) {
        lv_label_set_text_fmt(s_plot_readout, "RAW: %s %d\nADC calibration unavailable",
                     s_phase_plot ? "PHS" : "MAG",
                     s_phase_plot ? s_plot_sample.phase_raw : s_plot_sample.mag_raw);
    } else {
        char readout[96];
        snprintf(readout, sizeof(readout), "%lu Hz\n%s %.1f %s%s",
             (unsigned long)s_plot_sample.frequency_hz, s_phase_plot ? "PHS" : "MAG",
             s_phase_plot ? fmax(0, fmin(180, (1800 - s_plot_sample.phase_mv) / 10.0)) :
                    (s_plot_sample.mag_mv - 900) / 30.0,
             s_phase_plot ? "deg" : "dB",
                 (s_plot_sample.flags & AD8302_FLAG_RANGE_SUSPECT) ? " RANGE!" : "");
        lv_label_set_text(s_plot_readout, readout);
    }
}

static void update_screen(const analyzer_status_t *status)
{
    if (s_plot_page) {
        update_plot();
        return;
    }
    lv_label_set_text_fmt(s_status_label, "%s  /  %s\nNVS: %s",
                          s_simulated ? "SIM" : "ADC NOMINAL", status->busy ? "BUSY" : "READY",
                          status->storage_ready ? "READY" : "FAULT");
    for (unsigned field = 0; field < 4; field++) {
        bool selected = s_field == field;
        lv_label_set_text_fmt(s_field_values[field], "%lu", (unsigned long)s_values[field]);
        lv_obj_set_style_bg_color(s_field_labels[field], lv_color_hex(selected ? 0xD5E9E4 : 0xFFFFFF), 0);
        lv_obj_set_style_bg_color(s_field_values[field], lv_color_hex(selected ? 0xD5E9E4 : 0xFFFFFF), 0);
        lv_obj_set_style_text_color(s_field_labels[field], lv_color_hex(selected ? 0x126B62 : 0x596C73), 0);
        lv_obj_set_style_text_color(s_field_values[field], lv_color_hex(selected ? 0x126B62 : 0x243238), 0);
    }
    lv_label_set_text_fmt(s_entry_label, "Value: %s", s_entry[0] ? s_entry : "_");
    lv_label_set_text(s_result_label, s_i2c_failures ? "FAULT: keypad I2C" : s_message);
    lv_obj_set_style_text_color(s_result_label, lv_color_hex(s_i2c_failures ||
                               strncmp(s_message, "ERR", 3) == 0 ? 0xB02020 : 0x14684A), 0);
}

static void keypad_task(void *arg)
{
    (void)arg;
    bool fault = false;
    while (1) {
        uint16_t keys;
        int event = 0;
        bool notify = false;
        esp_err_t ret = fault ? keypad_configure() : ESP_OK;
        if (ret == ESP_OK) ret = keypad_scan(&keys);
        if (ret != ESP_OK) {
            if (!fault) {
                ESP_LOGE(TAG, "Keypad I2C: %s", esp_err_to_name(ret));
                event = -1;
                notify = true;
            }
            fault = true;
            s_debounce = 0;
            s_candidate = 0;
        } else {
            if (fault) {
                s_stable = UINT16_MAX;
                fault = false;
                notify = true;
            }
            if (keys != s_candidate) {
                s_candidate = keys;
                s_debounce = 1;
            } else if (s_debounce < 2) {
                s_debounce++;
            }
            if (s_debounce == 2 && keys != s_stable) {
                bool released = s_stable == 0;
                s_stable = keys;
                if (released && keys != 0 && (keys & (keys - 1)) == 0) {
                    static const char map[] = "123A456B789C*0#D";
                    unsigned index = 0;
                    while ((keys & (1U << index)) == 0) index++;
                    event = map[index];
                    notify = true;
                }
            }
        }
        if (notify && xQueueSend(s_key_events, &event, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Keypad event queue full");
        }
        vTaskDelay(pdMS_TO_TICKS(fault ? 250 : 10));
    }
}

static void hmi_task(void *arg)
{
    (void)arg;
    analyzer_status_t status;
    analyzer_get_status(&status);
    load_fields(&status);
    sweep_config_t loaded_sweep = status.sweep;
    s_completion = status.local_completion;
    int64_t previous = esp_timer_get_time();
    int64_t redraw = 0;
    while (1) {
        int64_t now = esp_timer_get_time();
        uint32_t elapsed_ms = (uint32_t)((now - previous) / 1000);
        lv_tick_inc(elapsed_ms);
        previous += (int64_t)elapsed_ms * 1000;
        analyzer_get_status(&status);
        if (status.local_completion != s_completion) {
            s_completion = status.local_completion;
            s_running = false;
            snprintf(s_message, sizeof(s_message), "%s", status.local_result);
        }
        if (!s_plot_page && !s_running && s_entry[0] == '\0' && !s_entry_overflow &&
            (status.sweep.start_hz != loaded_sweep.start_hz ||
             status.sweep.stop_hz != loaded_sweep.stop_hz ||
             status.sweep.step_hz != loaded_sweep.step_hz ||
             status.sweep.dwell_ms != loaded_sweep.dwell_ms ||
             status.sweep.point_count != loaded_sweep.point_count ||
             status.sweep.total_time_ms != loaded_sweep.total_time_ms ||
             status.sweep.measurement_source != loaded_sweep.measurement_source)) {
            unsigned selected = s_field;
            load_fields(&status);
            s_field = selected;
            loaded_sweep = status.sweep;
            snprintf(s_message, sizeof(s_message), "SETTINGS SYNCED");
        }
        int event;
        while (xQueueReceive(s_key_events, &event, 0) == pdTRUE) {
            s_i2c_failures = event < 0;
            if (event > 0) handle_key((char)event, &status);
        }
        if (now >= redraw) {
            update_screen(&status);
            redraw = now + 100000;
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(s_i2c_failures ? 250 : 10));
    }
}

static void release_hmi(void)
{
    if (s_keypad_task != NULL) vTaskDelete(s_keypad_task);
    if (s_key_events != NULL) vQueueDelete(s_key_events);
    s_keypad_task = NULL;
    s_key_events = NULL;
    if (s_panel_io != NULL) esp_lcd_panel_io_del(s_panel_io);
    if (s_panel != NULL) esp_lcd_panel_del(s_panel);
    if (s_display != NULL) lv_display_delete(s_display);
    if (s_spi_ready) spi_bus_free(SPI2_HOST);
    if (s_keypad != NULL) i2c_master_bus_rm_device(s_keypad);
    if (s_i2c_bus != NULL) i2c_del_master_bus(s_i2c_bus);
    heap_caps_free(s_pixels);
    s_panel_io = NULL;
    s_panel = NULL;
    s_display = NULL;
    s_pixels = NULL;
    s_spi_ready = false;
    s_keypad = NULL;
    s_i2c_bus = NULL;
}

esp_err_t local_hmi_init(void)
{
    if (s_i2c_bus != NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = keypad_init();
    if (ret == ESP_OK) ret = display_init();
    if (ret != ESP_OK) {
        release_hmi();
        return ret;
    }
    create_screen();
    s_key_events = xQueueCreate(16, sizeof(int));
    if (s_key_events == NULL ||
        xTaskCreate(keypad_task, "hmi_keypad", 3072, NULL, 2, &s_keypad_task) != pdPASS) {
        release_hmi();
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(hmi_task, "local_hmi", 6144, NULL, 2, NULL) != pdPASS) {
        release_hmi();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "HMI started: SPI2 TFT, I2C0 keypad; provisional pinout");
    return ESP_OK;
}