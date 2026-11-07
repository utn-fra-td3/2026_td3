#include "uart_console.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ad9834_driver.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_sweep.h"

#define UART_PORT UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_BUFFER_SIZE 256
#define UART_TASK_STACK 4096
#define UART_TX_PIN GPIO_NUM_1
#define UART_RX_PIN GPIO_NUM_3

static const char *TAG = "UART_CONSOLE";
static char s_line_buffer[UART_BUFFER_SIZE];
static size_t s_line_index = 0;
static uint32_t s_current_frequency_hz = 1000U;
static char s_current_wave[8] = "SINE";

static void uart_send_line(const char *msg)
{
    if (msg == NULL) {
        return;
    }

    uart_write_bytes(UART_PORT, msg, strlen(msg));
    uart_write_bytes(UART_PORT, "\r\n", 2);
}

static void uart_send_ok(const char *msg)
{
    char buffer[UART_BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "OK:%s", msg);
    uart_send_line(buffer);
}

static void uart_send_error(const char *msg)
{
    char buffer[UART_BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "ERR:%s", msg);
    uart_send_line(buffer);
}

static esp_err_t read_adc_channel_raw(uint32_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ret = adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_cfg);
    if (ret == ESP_OK) {
        int raw = 0;
        ret = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw);
        if (ret == ESP_OK) {
            *value = (uint32_t)raw;
        }
    }

    adc_oneshot_del_unit(adc_handle);
    return ret;
}

static void handle_status(void)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "FREQ=%lu,MODE=%s,STATUS=READY",
             (unsigned long)s_current_frequency_hz, s_current_wave);
    uart_send_ok(msg);
}

static void handle_help(void)
{
    uart_send_line("COMMANDS:");
    uart_send_line("  PING");
    uart_send_line("  STATUS");
    uart_send_line("  SETF <Hz>");
    uart_send_line("  SETW <SINE|TRI|SQUARE>");
    uart_send_line("  ADC");
    uart_send_line("  SWEEP <start> <stop> <step> <dwell_ms>");
    uart_send_line("  HELP");
}

static void handle_set_frequency(char *args)
{
    if (args == NULL) {
        uart_send_error("SETF requires frequency in Hz");
        return;
    }

    char *end = NULL;
    long freq = strtol(args, &end, 10);
    if (end == args || freq <= 0 ||
        (uint32_t)freq > ad9834_get_max_frequency_hz()) {
        uart_send_error("invalid frequency");
        return;
    }

    ad9834_set_frequency_hz((uint32_t)freq, AD9834_FREQ_REG0);
    s_current_frequency_hz = (uint32_t)freq;
    char msg[64];
    snprintf(msg, sizeof(msg), "SETF=%ld", freq);
    uart_send_ok(msg);
}

static void handle_set_wave(char *args)
{
    if (args == NULL) {
        uart_send_error("SETW requires <SINE|TRI|SQUARE>");
        return;
    }

    while (*args == ' ' || *args == '\t') {
        args++;
    }

    for (char *p = args; *p; ++p) {
        *p = (char)tolower((unsigned char)*p);
    }

    if (strcmp(args, "sine") == 0) {
        ad9834_set_output_sine();
        snprintf(s_current_wave, sizeof(s_current_wave), "SINE");
        uart_send_ok("WAVE=SINE");
    } else if (strcmp(args, "tri") == 0 || strcmp(args, "triangle") == 0) {
        ad9834_set_output_triangle();
        snprintf(s_current_wave, sizeof(s_current_wave), "TRI");
        uart_send_ok("WAVE=TRIANGLE");
    } else if (strcmp(args, "square") == 0) {
        ad9834_set_output_square();
        snprintf(s_current_wave, sizeof(s_current_wave), "SQUARE");
        uart_send_ok("WAVE=SQUARE");
    } else {
        uart_send_error("invalid waveform");
    }
}

static void handle_adc(void)
{
    uint32_t value = 0;
    esp_err_t ret = read_adc_channel_raw(&value);
    if (ret != ESP_OK) {
        char err[64];
        snprintf(err, sizeof(err), "ADC failed: %s", esp_err_to_name(ret));
        uart_send_error(err);
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "ADC=%lu", (unsigned long)value);
    uart_send_ok(msg);
}

static void handle_sweep(char *args)
{
    if (args == NULL) {
        uart_send_error("SWEEP requires start stop step dwell_ms");
        return;
    }

    char *save = NULL;
    char *token = strtok_r(args, " \t", &save);
    if (token == NULL) {
        uart_send_error("missing start");
        return;
    }
    char *tok_start = token;

    token = strtok_r(NULL, " \t", &save);
    if (token == NULL) {
        uart_send_error("missing stop");
        return;
    }
    char *tok_stop = token;

    token = strtok_r(NULL, " \t", &save);
    if (token == NULL) {
        uart_send_error("missing step");
        return;
    }
    char *tok_step = token;

    token = strtok_r(NULL, " \t", &save);
    if (token == NULL) {
        uart_send_error("missing dwell_ms");
        return;
    }

    char *end = NULL;
    uint32_t start_hz = (uint32_t)strtoul(tok_start, &end, 10);
    if (end == tok_start || start_hz == 0U) {
        uart_send_error("invalid start");
        return;
    }

    uint32_t stop_hz = (uint32_t)strtoul(tok_stop, &end, 10);
    if (end == tok_stop || stop_hz == 0U) {
        uart_send_error("invalid stop");
        return;
    }

    uint32_t step_hz = (uint32_t)strtoul(tok_step, &end, 10);
    if (end == tok_step || step_hz == 0U) {
        uart_send_error("invalid step");
        return;
    }

    uint32_t dwell_ms = (uint32_t)strtoul(token, &end, 10);
    if (end == token) {
        uart_send_error("invalid dwell_ms");
        return;
    }

    uint32_t max_frequency_hz = ad9834_get_max_frequency_hz();
    if (start_hz > max_frequency_hz || stop_hz > max_frequency_hz ||
        step_hz > max_frequency_hz || start_hz > stop_hz) {
        uart_send_error("sweep frequency outside DDS range");
        return;
    }

    sweep_config_t cfg = {
        .start_hz = start_hz,
        .stop_hz = stop_hz,
        .step_hz = step_hz,
        .dwell_ms = dwell_ms,
        .adc_channel = ADC_CHANNEL_0,
    };

    esp_err_t ret = sweep_run(&cfg);
    if (ret != ESP_OK) {
        char err[64];
        snprintf(err, sizeof(err), "SWEEP failed: %s", esp_err_to_name(ret));
        uart_send_error(err);
        return;
    }

    uart_send_ok("SWEEP_DONE");
}

static void dispatch_line(char *line)
{
    if (line == NULL) {
        return;
    }

    while (*line == ' ' || *line == '\t') {
        line++;
    }

    if (*line == '\0') {
        return;
    }

    char *save = NULL;
    char *command = strtok_r(line, " \t\r\n", &save);
    if (command == NULL) {
        return;
    }

    for (char *p = command; *p; ++p) {
        *p = (char)toupper((unsigned char)*p);
    }

    if (strcmp(command, "PING") == 0) {
        uart_send_ok("PONG");
    } else if (strcmp(command, "STATUS") == 0) {
        handle_status();
    } else if (strcmp(command, "HELP") == 0) {
        handle_help();
    } else if (strcmp(command, "SETF") == 0) {
        handle_set_frequency(save);
    } else if (strcmp(command, "SETW") == 0) {
        handle_set_wave(save);
    } else if (strcmp(command, "ADC") == 0) {
        handle_adc();
    } else if (strcmp(command, "SWEEP") == 0) {
        handle_sweep(save);
    } else {
        uart_send_error("unknown command");
    }
}

void uart_console_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(UART_PORT, UART_BUFFER_SIZE * 2, UART_BUFFER_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo instalar UART: %s", esp_err_to_name(ret));
        return;
    }

    ret = uart_param_config(UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar UART: %s", esp_err_to_name(ret));
        return;
    }

    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_line_inverse(UART_PORT, 0);

    ESP_LOGI(TAG, "UART console inicializada en %d bauds", UART_BAUD_RATE);
    uart_send_line("ESP32 UART console ready");
    uart_send_line("Type HELP for commands");
}

void uart_console_task(void *arg)
{
    (void)arg;

    while (1) {
        uint8_t byte = 0;
        int len = uart_read_bytes(UART_PORT, &byte, 1, pdMS_TO_TICKS(10));
        if (len <= 0) {
            continue;
        }

        if (byte == '\r' || byte == '\n') {
            if (s_line_index > 0) {
                s_line_buffer[s_line_index] = '\0';
                dispatch_line(s_line_buffer);
                s_line_index = 0;
            }
            continue;
        }

        if (s_line_index < (UART_BUFFER_SIZE - 1)) {
            s_line_buffer[s_line_index++] = (char)byte;
        }
    }
}
