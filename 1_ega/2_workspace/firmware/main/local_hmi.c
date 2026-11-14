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
static lv_obj_t *s_fields;
static lv_obj_t *s_entry_label;
static lv_obj_t *s_result_label;
static lv_obj_t *s_plot_heading;
static lv_obj_t *s_plot_status;
static lv_obj_t *s_plot_readout;
static lv_obj_t *s_frequency_labels[3];
static lv_obj_t *s_charts[2];
static lv_chart_series_t *s_series[2];
static bool s_plot_page;
static bool s_running;
static uint32_t s_received;
static ad8302_sample_t s_plot_sample;
static unsigned s_field;
static uint32_t s_values[4];
static bool s_simulated = true;
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
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF2F4F5), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x182028), 0);
    lv_obj_set_style_text_letter_space(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_home = make_page();
    lv_label_set_text(make_label(s_home, 8, 8, 224, 22), "FRA / SINE SWEEP");
    s_status_label = make_label(s_home, 8, 36, 224, 36);
    s_fields = make_label(s_home, 8, 82, 224, 88);
    s_entry_label = make_label(s_home, 8, 176, 224, 22);
    lv_label_set_text(make_label(s_home, 8, 202, 224, 34), "Points: 1..256\nTime: at least 10 ms/point");
    s_result_label = make_label(s_home, 8, 240, 224, 34);
    lv_label_set_text(make_label(s_home, 8, 280, 224, 36),
                      "B Next  C Source  D " LV_SYMBOL_PLAY "\n* " LV_SYMBOL_BACKSPACE "   # Save");
    s_plot = make_page();
    s_plot_heading = make_label(s_plot, 8, 4, 224, 20);
    s_plot_status = make_label(s_plot, 8, 26, 224, 20);
    lv_label_set_text(make_label(s_plot, 8, 48, 224, 18), "Gain dB [-30, +30]");
    lv_label_set_text(make_label(s_plot, 8, 146, 224, 18), "Phase |deg| [0, 180]");
    static const char *axis_labels[2][3] = {{"+30", "0", "-30"}, {"180", "90", "0"}};
    for (unsigned chart = 0; chart < 2; chart++) {
        int top = chart == 0 ? 68 : 166;
        s_charts[chart] = lv_chart_create(s_plot);
        lv_obj_set_pos(s_charts[chart], 42, top);
        lv_obj_set_size(s_charts[chart], 190, 68);
        lv_obj_set_style_pad_all(s_charts[chart], 0, 0);
        lv_obj_set_style_radius(s_charts[chart], 0, 0);
        lv_obj_set_style_size(s_charts[chart], 3, 3, LV_PART_INDICATOR);
        lv_chart_set_type(s_charts[chart], LV_CHART_TYPE_SCATTER);
        lv_chart_set_point_count(s_charts[chart], ANALYZER_LOCAL_MAX_POINTS);
        lv_chart_set_range(s_charts[chart], LV_CHART_AXIS_PRIMARY_X, 0, 1000);
        lv_chart_set_range(s_charts[chart], LV_CHART_AXIS_PRIMARY_Y,
                           chart == 0 ? -300 : 0, chart == 0 ? 300 : 1800);
        lv_chart_set_div_line_count(s_charts[chart], 3, 3);
        s_series[chart] = lv_chart_add_series(s_charts[chart],
                              lv_color_hex(chart == 0 ? 0x126B62 : 0xB13C54), LV_CHART_AXIS_PRIMARY_Y);
        for (unsigned tick = 0; tick < 3; tick++) {
            lv_label_set_text(make_label(s_plot, 4, top + tick * 25, 36, 18), axis_labels[chart][tick]);
        }
    }
    for (unsigned tick = 0; tick < 3; tick++) {
        s_frequency_labels[tick] = make_label(s_plot, 8 + tick * 76, 236, 72, 18);
        lv_obj_set_style_text_align(s_frequency_labels[tick],
                                   tick == 0 ? LV_TEXT_ALIGN_LEFT : (tick == 1 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT), 0);
    }
    s_plot_readout = make_label(s_plot, 8, 256, 224, 36);
    lv_label_set_text(make_label(s_plot, 8, 298, 224, 18), "A " LV_SYMBOL_HOME "  /  Stop");
    lv_obj_add_flag(s_plot, LV_OBJ_FLAG_HIDDEN);
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
    s_values[2] = count == 0 ? 100 : (count > ANALYZER_LOCAL_MAX_POINTS ? ANALYZER_LOCAL_MAX_POINTS : count);
    s_values[3] = status->sweep.total_time_ms;
    if (s_values[3] == 0) {
        uint32_t dwell = status->sweep.dwell_ms < 10 ? 10 : status->sweep.dwell_ms;
        uint64_t duration = (uint64_t)s_values[2] * dwell;
        s_values[3] = duration > 3600000 ? 3600000 : (uint32_t)duration;
    }
}

static bool commit_entry(void)
{
    if (s_entry_overflow) {
        snprintf(s_message, sizeof(s_message), "ERR: entry exceeds 10 digits");
        return false;
    }
    if (s_entry[0] == '\0') return true;
    unsigned long long value = strtoull(s_entry, NULL, 10);
    if (value == 0 || value > UINT32_MAX) {
        snprintf(s_message, sizeof(s_message), "ERR: value out of range");
        return false;
    }
    if (s_field < 2 && (value < AD9834_MIN_FREQUENCY_HZ ||
                        value > ad9834_get_max_frequency_hz())) {
        snprintf(s_message, sizeof(s_message), "ERR: frequency 100k..37.5M Hz");
        return false;
    }
    s_values[s_field] = (uint32_t)value;
    s_entry[0] = '\0';
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

static void handle_key(char key, const analyzer_status_t *status)
{
    (void)status;
    if (key == 'A') {
        show_home();
        return;
    }
    if (s_plot_page) return;
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
        commit_entry();
        return;
    }
    if (key == 'B') {
        if (commit_entry()) s_field = (s_field + 1) % 4;
        return;
    }
    if (key == 'C') {
        s_simulated = !s_simulated;
        return;
    }
    if (key == 'D' && commit_entry()) {
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
        s_running = true;
        s_plot_page = true;
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
        if (!(s_plot_sample.flags & AD8302_FLAG_ADC_UNCALIBRATED) &&
            s_plot_sample.mag_mv >= 0 && s_plot_sample.phase_mv >= 0) {
            double span = log10((double)s_values[1] / s_values[0]);
            int32_t position = span > 0 ? (int32_t)lround(1000 * log10((double)s_plot_sample.frequency_hz / s_values[0]) / span) : 500;
            lv_chart_get_x_array(s_charts[0], s_series[0])[s_received] = position;
            lv_chart_get_x_array(s_charts[1], s_series[1])[s_received] = position;
            lv_chart_get_y_array(s_charts[0], s_series[0])[s_received] = (s_plot_sample.mag_mv - 900) / 3;
            int32_t phase = 1800 - s_plot_sample.phase_mv;
            lv_chart_get_y_array(s_charts[1], s_series[1])[s_received] = phase < 0 ? 0 : (phase > 1800 ? 1800 : phase);
        }
        s_received++;
    }
    if (s_received != before) {
        for (unsigned chart = 0; chart < 2; chart++) lv_chart_refresh(s_charts[chart]);
    }
    lv_label_set_text(s_plot_heading, s_simulated ? "SINE / SIM / log Hz" : "SINE / ADC NOM / log Hz");
    lv_label_set_text_fmt(s_plot_status, "%s  %lu/%lu", s_running ? "RUN" :
                           (strncmp(s_message, "ERR", 3) == 0 ? "ERROR" : "DONE"),
                           (unsigned long)s_received, (unsigned long)s_values[2]);
    if (s_i2c_failures || strncmp(s_message, "ERR", 3) == 0) {
        lv_label_set_text(s_plot_readout, s_i2c_failures ? "FAULT: keypad I2C" : s_message);
    } else if (s_received == 0) {
        lv_label_set_text(s_plot_readout, "Waiting for samples");
    } else if (s_plot_sample.flags & AD8302_FLAG_ADC_UNCALIBRATED) {
        lv_label_set_text_fmt(s_plot_readout, "RAW: MAG %d  PH %d\nADC calibration unavailable",
                             s_plot_sample.mag_raw, s_plot_sample.phase_raw);
    } else {
        char readout[96];
        snprintf(readout, sizeof(readout), "%lu Hz  %+.1f dB\n|Phase| %.1f deg%s",
                 (unsigned long)s_plot_sample.frequency_hz, (s_plot_sample.mag_mv - 900) / 30.0,
                 fmax(0, fmin(180, (1800 - s_plot_sample.phase_mv) / 10.0)),
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
    lv_label_set_text_fmt(s_fields, "%c Start Hz: %lu\n%c Stop Hz: %lu\n%c Points: %lu\n%c Total ms: %lu",
                         s_field == 0 ? '>' : ' ', (unsigned long)s_values[0],
                         s_field == 1 ? '>' : ' ', (unsigned long)s_values[1],
                         s_field == 2 ? '>' : ' ', (unsigned long)s_values[2],
                         s_field == 3 ? '>' : ' ', (unsigned long)s_values[3]);
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