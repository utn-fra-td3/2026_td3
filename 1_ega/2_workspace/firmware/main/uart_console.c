#include "uart_console.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad9834_driver.h"
#include "ad8302_measurement.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "frequency_sweep.h"

#define UART_PORT UART_NUM_2
#define UART_BAUD_RATE 115200
#define UART_BUFFER_SIZE 256
#define UART_TASK_STACK 4096
#define UART_TX_PIN GPIO_NUM_25
#define UART_RX_PIN GPIO_NUM_26

static const char *TAG = "UART_CONSOLE";
static bool s_uart_ready = false;
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
    return ad8302_read_raw(AD8302_MAG_CHANNEL, value);
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
    uart_send_line("  MEAS <SIM|ADC>");
    uart_send_line("  MSWEEP <SIM|ADC> <start> <stop> <step> <dwell_ms>");
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

static void send_sweep_sample(uint32_t frequency_hz, uint32_t adc_value, void *context)
{
    (void)context;
    s_current_frequency_hz = frequency_hz;
    char msg[64];
    snprintf(msg, sizeof(msg), "f = %lu Hz | ADC = %lu",
             (unsigned long)frequency_hz, (unsigned long)adc_value);
    uart_send_line(msg);
}

static void send_measurement_sample(const ad8302_sample_t *sample, uint32_t sequence, void *context)
{
    (void)context;
    bool simulated = (sample->flags & AD8302_FLAG_SIMULATED) != 0;
    if (!simulated) {
        s_current_frequency_hz = sample->frequency_hz;
    }
    char msg[UART_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "DATA:V1,F=%lu,SEQ=%lu,T_US=%lld,SOURCE=%s,MAG_RAW=%d,PHASE_RAW=%d,"
             "MAG_MV=%d,PHASE_MV=%d,MAG_SPAN=%d,PHASE_SPAN=%d,FLAGS=%lu",
             (unsigned long)sample->frequency_hz, (unsigned long)sequence,
             (long long)sample->timestamp_us, simulated ? "SIM" : "ADC",
             sample->mag_raw, sample->phase_raw, sample->mag_mv, sample->phase_mv,
             sample->mag_span_raw, sample->phase_span_raw, (unsigned long)sample->flags);
    uart_send_line(msg);
}

static bool parse_source(const char *token, ad8302_source_t *source)
{
    if (token != NULL && strcmp(token, "SIM") == 0) {
        *source = AD8302_SOURCE_SIM;
        return true;
    }
    if (token != NULL && strcmp(token, "ADC") == 0) {
        *source = AD8302_SOURCE_ADC;
        return true;
    }
    return false;
}

static void handle_measurement(char *args)
{
    char *save = NULL;
    ad8302_source_t source;
    char *token = args != NULL ? strtok_r(args, " \t", &save) : NULL;
    if (!parse_source(token, &source) || strtok_r(NULL, " \t", &save) != NULL) {
        uart_send_error("MEAS requires SIM or ADC");
        return;
    }
    if (source == AD8302_SOURCE_ADC && strcmp(s_current_wave, "SINE") != 0) {
        uart_send_error("AD8302 measurement requires SINE");
        return;
    }
    if (source == AD8302_SOURCE_ADC) {
        vTaskDelay(pdMS_TO_TICKS(50) > 0 ? pdMS_TO_TICKS(50) : 1);
    }
    ad8302_sample_t sample;
    esp_err_t ret = ad8302_measure(s_current_frequency_hz, source, &sample);
    if (ret != ESP_OK) {
        uart_send_error(esp_err_to_name(ret));
        return;
    }
    send_measurement_sample(&sample, 0, NULL);
    uart_send_ok("MEAS_DONE");
}

static bool parse_unsigned(const char *token, uint32_t *value)
{
    if (token == NULL || *token == '\0') {
        return false;
    }
    for (const char *digit = token; *digit != '\0'; digit++) {
        if (!isdigit((unsigned char)*digit)) {
            return false;
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(token, &end, 10);
    if (errno == ERANGE || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static void handle_sweep(char *args, bool measurement)
{
    if (args == NULL) {
        uart_send_error("sweep requires start stop step dwell_ms");
        return;
    }
    char *save = NULL;
    char *token = strtok_r(args, " \t", &save);
    ad8302_source_t source = AD8302_SOURCE_ADC;
    if (measurement && !parse_source(token, &source)) {
        uart_send_error("MSWEEP requires SIM or ADC source");
        return;
    }
    if (measurement) {
        token = strtok_r(NULL, " \t", &save);
    }
    uint32_t values[4];
    for (size_t index = 0; index < 4; index++) {
        if (!parse_unsigned(token, &values[index])) {
            uart_send_error("sweep requires four unsigned integers");
            return;
        }
        token = strtok_r(NULL, " \t", &save);
    }
    if (token != NULL) {
        uart_send_error("unexpected sweep argument");
        return;
    }
    uint32_t max_frequency_hz = ad9834_get_max_frequency_hz();
    if (values[0] == 0 || values[0] > values[1] || values[1] > max_frequency_hz ||
        values[2] == 0 || values[2] > max_frequency_hz || values[3] == 0 || values[3] > 60000) {
        uart_send_error("invalid sweep range, step or dwell (1..60000 ms)");
        return;
    }
    if (measurement && source == AD8302_SOURCE_ADC && strcmp(s_current_wave, "SINE") != 0) {
        uart_send_error("AD8302 measurement requires SINE");
        return;
    }

    sweep_config_t cfg = {
        .start_hz = values[0],
        .stop_hz = values[1],
        .step_hz = values[2],
        .dwell_ms = values[3],
        .adc_channel = AD8302_MAG_CHANNEL,
        .sample_callback = measurement ? NULL : send_sweep_sample,
        .sample_context = NULL,
        .measurement_source = source,
        .measurement_callback = measurement ? send_measurement_sample : NULL,
    };

    esp_err_t ret = sweep_run(&cfg);
    if (ret != ESP_OK) {
        char err[64];
        snprintf(err, sizeof(err), "SWEEP failed: %s", esp_err_to_name(ret));
        uart_send_error(err);
        return;
    }

    uart_send_ok(measurement ? "MSWEEP_DONE" : "SWEEP_DONE");
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
        handle_sweep(save, false);
    } else if (strcmp(command, "MEAS") == 0) {
        handle_measurement(save);
    } else if (strcmp(command, "MSWEEP") == 0) {
        handle_sweep(save, true);
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
        uart_driver_delete(UART_PORT);
        return;
    }

    ret = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret == ESP_OK) {
        ret = uart_set_line_inverse(UART_PORT, 0);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudieron configurar pines UART: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT);
        return;
    }

    s_uart_ready = true;
    ESP_LOGI(TAG, "UART2 console: TX GPIO%d, RX GPIO%d, %d bauds", UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
    uart_send_line("ESP32 UART console ready");
    uart_send_line("Type HELP for commands");
}

void uart_console_task(void *arg)
{
    (void)arg;

    if (!s_uart_ready) {
        vTaskDelete(NULL);
        return;
    }

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
