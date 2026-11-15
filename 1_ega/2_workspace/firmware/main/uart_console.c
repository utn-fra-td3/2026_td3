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
#include "freertos/queue.h"
#include "freertos/task.h"
#include "frequency_sweep.h"
#include "nvs.h"
#include "nvs_flash.h"

#define UART_PORT UART_NUM_2
#define UART_BAUD_RATE 115200
#define UART_BUFFER_SIZE 256
#define UART_TASK_STACK 4096
#define UART_TX_PIN GPIO_NUM_25
#define UART_RX_PIN GPIO_NUM_26
#define UART_EVENT_QUEUE_LENGTH 16
#define UART_TX_QUEUE_LENGTH 16

typedef struct {
    char text[UART_BUFFER_SIZE];
    bool local;
    bool sine_sweep;
} console_line_t;

typedef struct {
    uint32_t version;
    uint32_t frequency_hz;
    uint32_t waveform;
    uint32_t start_hz;
    uint32_t stop_hz;
    uint32_t step_hz;
    uint32_t dwell_ms;
    uint32_t point_count;
    uint32_t total_time_ms;
    uint32_t source;
} saved_settings_t;

static const char *TAG = "UART_CONSOLE";
static bool s_uart_ready = false;
static char s_line_buffer[UART_BUFFER_SIZE];
static size_t s_line_index = 0;
static bool s_discard_line = false;
static uint32_t s_current_frequency_hz = AD9834_MIN_FREQUENCY_HZ;
static char s_current_wave[8] = "SINE";
static bool s_device_busy = false;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_uart_events;
static QueueHandle_t s_commands;
static QueueHandle_t s_responses;
static TaskHandle_t s_rx_task;
static TaskHandle_t s_device_task;
static TaskHandle_t s_tx_task;
static console_line_t s_device_completion;
static bool s_completion_pending;
static nvs_handle_t s_settings_store;
static bool s_storage_ready;
static bool s_settings_restored;
static bool s_local_command;
static bool s_local_sweep;
static bool s_cancel_requested;
static ad8302_sample_t s_local_samples[ANALYZER_LOCAL_MAX_POINTS];
static uint32_t s_local_sample_count;
static uint32_t s_local_completion;
static char s_local_result[64];
static bool s_sample_valid;
static ad8302_sample_t s_latest_sample;
static uint32_t s_sample_sequence;
static sweep_config_t s_last_sweep = {
    .start_hz = AD9834_MIN_FREQUENCY_HZ,
    .stop_hz = AD9834_MAX_FREQUENCY_HZ,
    .point_count = 100U,
    .total_time_ms = 10000U,
    .adc_channel = AD8302_MAG_CHANNEL,
    .measurement_source = AD8302_SOURCE_ADC,
};

static void set_current_frequency(uint32_t frequency_hz)
{
    portENTER_CRITICAL(&s_state_lock);
    s_current_frequency_hz = frequency_hz;
    portEXIT_CRITICAL(&s_state_lock);
}

static void set_current_wave(const char *wave)
{
    portENTER_CRITICAL(&s_state_lock);
    memcpy(s_current_wave, wave, strlen(wave) + 1);
    portEXIT_CRITICAL(&s_state_lock);
}

static esp_err_t persist_configuration(void)
{
    if (s_settings_store == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    saved_settings_t settings = {
        .version = 1U,
        .frequency_hz = s_current_frequency_hz,
        .waveform = strcmp(s_current_wave, "SINE") == 0 ? 0U : (strcmp(s_current_wave, "TRI") == 0 ? 1U : 2U),
        .start_hz = s_last_sweep.start_hz,
        .stop_hz = s_last_sweep.stop_hz,
        .step_hz = s_last_sweep.step_hz,
        .dwell_ms = s_last_sweep.dwell_ms,
        .point_count = s_last_sweep.point_count,
        .total_time_ms = s_last_sweep.total_time_ms,
        .source = (uint32_t)s_last_sweep.measurement_source,
    };
    esp_err_t ret = nvs_set_blob(s_settings_store, "config", &settings, sizeof(settings));
    if (ret == ESP_OK) {
        ret = nvs_commit(s_settings_store);
    }
    portENTER_CRITICAL(&s_state_lock);
    s_storage_ready = ret == ESP_OK;
    portEXIT_CRITICAL(&s_state_lock);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Settings save failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void restore_configuration(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_OK) {
        ret = nvs_open("ega_analyzer", NVS_READWRITE, &s_settings_store);
    }
    s_storage_ready = ret == ESP_OK;
    if (ret == ESP_OK) {
        saved_settings_t settings = {0};
        size_t size = sizeof(settings);
        ret = nvs_get_blob(s_settings_store, "config", &settings, &size);
        if (ret == ESP_OK && size == sizeof(settings) && settings.version == 1U &&
            settings.frequency_hz >= AD9834_MIN_OUTPUT_FREQUENCY_HZ && settings.frequency_hz <= ad9834_get_max_frequency_hz() &&
            settings.waveform <= 2U) {
            sweep_config_t restored = {
                .start_hz = settings.start_hz,
                .stop_hz = settings.stop_hz,
                .step_hz = settings.step_hz,
                .dwell_ms = settings.dwell_ms,
                .point_count = settings.point_count,
                .total_time_ms = settings.total_time_ms,
                .adc_channel = AD8302_MAG_CHANNEL,
                .measurement_source = (ad8302_source_t)settings.source,
            };
            if (sweep_validate(&restored) == ESP_OK) {
                s_last_sweep = restored;
                set_current_frequency(settings.frequency_hz);
                set_current_wave(settings.waveform == 0U ? "SINE" : (settings.waveform == 1U ? "TRI" : "SQUARE"));
                s_settings_restored = true;
            }
        }
        if (!s_settings_restored && ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Saved settings invalid or unreadable; using defaults");
        }
    } else {
        ESP_LOGE(TAG, "NVS unavailable (not erased): %s", esp_err_to_name(ret));
    }
    if (strcmp(s_current_wave, "TRI") == 0) {
        ad9834_set_output_triangle();
    } else if (strcmp(s_current_wave, "SQUARE") == 0) {
        ad9834_set_output_square();
    } else {
        ad9834_set_output_sine();
    }
    ad9834_set_frequency_hz(s_current_frequency_hz, AD9834_FREQ_REG0);
    ad9834_enable(true);
    ESP_LOGI(TAG, "Configuration %s; storage %s", s_settings_restored ? "restored" : "defaults",
             s_storage_ready ? "ready" : "fault");
}

static void uart_send_line(const char *msg)
{
    if (msg == NULL) {
        return;
    }

    if (xTaskGetCurrentTaskHandle() == s_device_task && s_local_command) {
        if (strncmp(msg, "OK:", 3) == 0 || strncmp(msg, "ERR:", 4) == 0) {
            portENTER_CRITICAL(&s_state_lock);
            snprintf(s_local_result, sizeof(s_local_result), "%s", msg);
            portEXIT_CRITICAL(&s_state_lock);
        }
        return;
    }

    console_line_t response = {0};
    snprintf(response.text, sizeof(response.text), "%s", msg);
    if (xTaskGetCurrentTaskHandle() == s_device_task &&
        (strncmp(msg, "OK:", 3) == 0 || strncmp(msg, "ERR:", 4) == 0)) {
        s_device_completion = response;
        s_completion_pending = true;
        return;
    }
    if (xQueueSend(s_responses, &response, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "UART response queue unavailable");
    }
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
    uint32_t frequency_hz;
    char wave[sizeof(s_current_wave)];
    bool busy;
    bool storage_ready;
    sweep_config_t sweep;
    portENTER_CRITICAL(&s_state_lock);
    frequency_hz = s_current_frequency_hz;
    memcpy(wave, s_current_wave, sizeof(wave));
    busy = s_device_busy;
    storage_ready = s_storage_ready;
    sweep = s_last_sweep;
    portEXIT_CRITICAL(&s_state_lock);
    char msg[UART_BUFFER_SIZE - 4];
    snprintf(msg, sizeof(msg), "FREQ=%lu,MODE=%s,STATUS=%s,STORAGE=%s,RESTORED=%u,"
             "START=%lu,STOP=%lu,STEP=%lu,DWELL_MS=%lu,POINTS=%lu,TOTAL_MS=%lu,SOURCE=%s",
             (unsigned long)frequency_hz, wave, busy ? "BUSY" : "READY",
             storage_ready ? "READY" : "FAULT", s_settings_restored ? 1U : 0U,
             (unsigned long)sweep.start_hz, (unsigned long)sweep.stop_hz,
             (unsigned long)sweep.step_hz, (unsigned long)sweep.dwell_ms,
             (unsigned long)sweep.point_count, (unsigned long)sweep.total_time_ms,
             sweep.measurement_source == AD8302_SOURCE_SIM ? "SIM" : "ADC");
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
    uart_send_line("  MSWEEP_POINTS <SIM|ADC> <start> <stop> <points> <total_ms>");
    uart_send_line("  SWEEP_CONFIG <SIM|ADC> <start> <stop> <points> <total_ms>");
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
    if (end == args || freq < AD9834_MIN_OUTPUT_FREQUENCY_HZ ||
        freq > ad9834_get_max_frequency_hz()) {
        uart_send_error("invalid frequency");
        return;
    }

    ad9834_set_frequency_hz((uint32_t)freq, AD9834_FREQ_REG0);
    set_current_frequency((uint32_t)freq);
    if (persist_configuration() != ESP_OK) {
        uart_send_error("settings applied but persistence failed");
        return;
    }
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
        set_current_wave("SINE");
    } else if (strcmp(args, "tri") == 0 || strcmp(args, "triangle") == 0) {
        ad9834_set_output_triangle();
        set_current_wave("TRI");
    } else if (strcmp(args, "square") == 0) {
        ad9834_set_output_square();
        set_current_wave("SQUARE");
    } else {
        uart_send_error("invalid waveform");
        return;
    }
    if (persist_configuration() != ESP_OK) {
        uart_send_error("settings applied but persistence failed");
        return;
    }
    char response[32];
    snprintf(response, sizeof(response), "WAVE=%s", strcmp(s_current_wave, "TRI") == 0 ? "TRIANGLE" : s_current_wave);
    uart_send_ok(response);
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
    set_current_frequency(frequency_hz);
    char msg[64];
    snprintf(msg, sizeof(msg), "f = %lu Hz | ADC = %lu",
             (unsigned long)frequency_hz, (unsigned long)adc_value);
    uart_send_line(msg);
}

static void send_measurement_sample(const ad8302_sample_t *sample, uint32_t sequence, void *context)
{
    (void)context;
    portENTER_CRITICAL(&s_state_lock);
    s_latest_sample = *sample;
    s_sample_sequence = sequence;
    s_sample_valid = true;
    if (s_local_sweep && s_local_sample_count < ANALYZER_LOCAL_MAX_POINTS) {
        s_local_samples[s_local_sample_count++] = *sample;
    }
    portEXIT_CRITICAL(&s_state_lock);
    bool simulated = (sample->flags & AD8302_FLAG_SIMULATED) != 0;
    if (!simulated) {
        set_current_frequency(sample->frequency_hz);
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

static bool local_sweep_cancelled(void *context)
{
    (void)context;
    portENTER_CRITICAL(&s_state_lock);
    bool cancelled = s_cancel_requested;
    portEXIT_CRITICAL(&s_state_lock);
    return cancelled;
}

static void handle_sweep(char *args, bool measurement, bool counted, bool configure_only)
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
    if (!configure_only && !s_local_sweep && measurement && source == AD8302_SOURCE_ADC && strcmp(s_current_wave, "SINE") != 0) {
        uart_send_error("AD8302 measurement requires SINE");
        return;
    }

    sweep_config_t cfg = {
        .start_hz = values[0],
        .stop_hz = values[1],
        .step_hz = counted ? 0U : values[2],
        .dwell_ms = counted ? 0U : values[3],
        .point_count = counted ? values[2] : 0U,
        .total_time_ms = counted ? values[3] : 0U,
        .adc_channel = AD8302_MAG_CHANNEL,
        .sample_callback = measurement ? NULL : send_sweep_sample,
        .sample_context = NULL,
        .measurement_source = source,
        .measurement_callback = measurement ? send_measurement_sample : NULL,
        .cancel_callback = s_local_sweep ? local_sweep_cancelled : NULL,
    };

    esp_err_t ret = sweep_validate(&cfg);
    if (ret != ESP_OK) {
        uart_send_error("invalid sweep range, points or timing");
        return;
    }
    if (s_local_sweep) {
        if (local_sweep_cancelled(NULL)) {
            uart_send_ok("SWEEP_CANCELLED");
            return;
        }
        ad9834_set_output_sine();
        set_current_wave("SINE");
    }
    portENTER_CRITICAL(&s_state_lock);
    s_last_sweep = cfg;
    s_last_sweep.cancel_callback = NULL;
    portEXIT_CRITICAL(&s_state_lock);
    if (persist_configuration() != ESP_OK) {
        uart_send_error("sweep settings persistence failed");
        return;
    }
    if (configure_only) {
        if (s_local_command) {
            uart_send_ok("SETTINGS UPDATED");
        } else {
            handle_status();
        }
        return;
    }
    ret = sweep_run(&cfg);
    if (ret == SWEEP_ERR_CANCELLED) {
        uart_send_ok("SWEEP_CANCELLED");
        return;
    }
    if (ret != ESP_OK) {
        char err[64];
        snprintf(err, sizeof(err), "SWEEP failed: %s", esp_err_to_name(ret));
        uart_send_error(err);
        return;
    }

    if (source == AD8302_SOURCE_ADC && persist_configuration() != ESP_OK) {
        uart_send_error("sweep finished but persistence failed");
        return;
    }
    uart_send_ok(counted ? "MSWEEP_POINTS_DONE" : (measurement ? "MSWEEP_DONE" : "SWEEP_DONE"));
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
        handle_sweep(save, false, false, false);
    } else if (strcmp(command, "MEAS") == 0) {
        handle_measurement(save);
    } else if (strcmp(command, "MSWEEP") == 0) {
        handle_sweep(save, true, false, false);
    } else if (strcmp(command, "MSWEEP_POINTS") == 0) {
        handle_sweep(save, true, true, false);
    } else if (strcmp(command, "SWEEP_CONFIG") == 0) {
        handle_sweep(save, true, true, true);
    } else {
        uart_send_error("unknown command");
    }
}

static esp_err_t enqueue_command(const console_line_t *request)
{
    if (!s_uart_ready || s_commands == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_state_lock);
    bool busy = s_device_busy;
    if (!busy) {
        s_device_busy = true;
        s_local_sweep = request->sine_sweep;
        s_cancel_requested = false;
        if (request->sine_sweep) s_local_sample_count = 0;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_commands, request, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_state_lock);
        s_device_busy = false;
        s_local_sweep = false;
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t analyzer_submit_command(const char *command)
{
    if (command == NULL || command[0] == '\0' || strlen(command) >= UART_BUFFER_SIZE ||
        strchr(command, '\r') != NULL || strchr(command, '\n') != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    console_line_t request = {.local = true};
    snprintf(request.text, sizeof(request.text), "%s", command);
    return enqueue_command(&request);
}

esp_err_t analyzer_start_local_sweep(const sweep_config_t *config)
{
    if (config == NULL || config->point_count == 0 ||
        config->point_count > ANALYZER_LOCAL_MAX_POINTS || sweep_validate(config) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    console_line_t request = {.local = true, .sine_sweep = true};
    snprintf(request.text, sizeof(request.text), "MSWEEP_POINTS %s %lu %lu %lu %lu",
             config->measurement_source == AD8302_SOURCE_SIM ? "SIM" : "ADC",
             (unsigned long)config->start_hz, (unsigned long)config->stop_hz,
             (unsigned long)config->point_count, (unsigned long)config->total_time_ms);
    return enqueue_command(&request);
}

bool analyzer_cancel_local_sweep(void)
{
    portENTER_CRITICAL(&s_state_lock);
    bool active = s_device_busy && s_local_sweep;
    if (active) s_cancel_requested = true;
    portEXIT_CRITICAL(&s_state_lock);
    return active;
}

bool analyzer_get_local_sample(uint32_t index, ad8302_sample_t *sample)
{
    if (sample == NULL) return false;
    portENTER_CRITICAL(&s_state_lock);
    bool available = index < s_local_sample_count;
    if (available) *sample = s_local_samples[index];
    portEXIT_CRITICAL(&s_state_lock);
    return available;
}

void analyzer_get_status(analyzer_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_state_lock);
    status->frequency_hz = s_current_frequency_hz;
    memcpy(status->wave, s_current_wave, sizeof(status->wave));
    status->busy = s_device_busy;
    status->storage_ready = s_storage_ready;
    status->sweep = s_last_sweep;
    status->local_completion = s_local_completion;
    memcpy(status->local_result, s_local_result, sizeof(status->local_result));
    status->sample_valid = s_sample_valid;
    status->sample = s_latest_sample;
    status->sequence = s_sample_sequence;
    portEXIT_CRITICAL(&s_state_lock);
}

static void submit_line(char *line)
{
    console_line_t request = {0};
    snprintf(request.text, sizeof(request.text), "%s", line);
    char *save = NULL;
    char *command = strtok_r(line, " \t\r\n", &save);
    if (command == NULL) {
        return;
    }
    for (char *character = command; *character; character++) {
        *character = (char)toupper((unsigned char)*character);
    }
    if (strcmp(command, "PING") == 0) {
        uart_send_ok("PONG");
        return;
    }
    if (strcmp(command, "STATUS") == 0) {
        handle_status();
        return;
    }
    if (strcmp(command, "HELP") == 0) {
        handle_help();
        return;
    }
    if (strcmp(command, "SETF") != 0 && strcmp(command, "SETW") != 0 &&
        strcmp(command, "ADC") != 0 && strcmp(command, "SWEEP") != 0 &&
        strcmp(command, "MEAS") != 0 && strcmp(command, "MSWEEP") != 0 &&
        strcmp(command, "MSWEEP_POINTS") != 0 && strcmp(command, "SWEEP_CONFIG") != 0) {
        uart_send_error("unknown command");
        return;
    }
    esp_err_t ret = enqueue_command(&request);
    if (ret == ESP_ERR_INVALID_STATE) {
        uart_send_error("BUSY");
    } else if (ret != ESP_OK) {
        uart_send_error("command queue full");
    }
}

static void device_command_task(void *arg)
{
    (void)arg;
    console_line_t request;
    while (1) {
        if (xQueueReceive(s_commands, &request, portMAX_DELAY) == pdTRUE) {
            s_completion_pending = false;
            s_local_command = request.local;
            dispatch_line(request.text);
            portENTER_CRITICAL(&s_state_lock);
            if (request.local) {
                s_local_completion++;
            }
            s_device_busy = false;
            s_local_sweep = false;
            portEXIT_CRITICAL(&s_state_lock);
            if (s_completion_pending) {
                xQueueSend(s_responses, &s_device_completion, portMAX_DELAY);
            }
        }
    }
}

static void uart_response_task(void *arg)
{
    (void)arg;
    console_line_t response;
    char frame[UART_BUFFER_SIZE + 2];
    while (1) {
        if (xQueueReceive(s_responses, &response, portMAX_DELAY) == pdTRUE) {
            int length = snprintf(frame, sizeof(frame), "%s\r\n", response.text);
            if (uart_write_bytes(UART_PORT, frame, length) != length) {
                ESP_LOGE(TAG, "UART transmission failed");
            }
        }
    }
}

static void console_release_resources(void)
{
    if (s_rx_task != NULL) vTaskDelete(s_rx_task);
    if (s_device_task != NULL) vTaskDelete(s_device_task);
    if (s_tx_task != NULL) vTaskDelete(s_tx_task);
    s_rx_task = NULL;
    s_device_task = NULL;
    s_tx_task = NULL;
    if (s_commands != NULL) vQueueDelete(s_commands);
    if (s_responses != NULL) vQueueDelete(s_responses);
    s_commands = NULL;
    s_responses = NULL;
    if (s_settings_store != 0) {
        nvs_close(s_settings_store);
        s_settings_store = 0;
    }
    s_uart_ready = false;
}

esp_err_t uart_console_init(void)
{
    if (s_uart_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    s_commands = xQueueCreate(1, sizeof(console_line_t));
    s_responses = xQueueCreate(UART_TX_QUEUE_LENGTH, sizeof(console_line_t));
    if (s_commands == NULL || s_responses == NULL) {
        console_release_resources();
        return ESP_ERR_NO_MEM;
    }
    restore_configuration();
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(UART_PORT, UART_BUFFER_SIZE * 4, UART_BUFFER_SIZE * 2,
                                       UART_EVENT_QUEUE_LENGTH, &s_uart_events, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo instalar UART: %s", esp_err_to_name(ret));
        console_release_resources();
        return ret;
    }

    ret = uart_param_config(UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo configurar UART: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT);
        s_uart_events = NULL;
        console_release_resources();
        return ret;
    }

    ret = uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret == ESP_OK) {
        ret = uart_set_line_inverse(UART_PORT, 0);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudieron configurar pines UART: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT);
        s_uart_events = NULL;
        console_release_resources();
        return ret;
    }

    s_uart_ready = true;
    if (xTaskCreate(uart_response_task, "uart_tx", UART_TASK_STACK, NULL, 4, &s_tx_task) != pdPASS ||
        xTaskCreate(device_command_task, "analyzer", UART_TASK_STACK, NULL, 3, &s_device_task) != pdPASS ||
        xTaskCreate(uart_console_task, "uart_rx", UART_TASK_STACK, NULL, 5, &s_rx_task) != pdPASS) {
        console_release_resources();
        uart_driver_delete(UART_PORT);
        s_uart_events = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UART2 console: TX GPIO%d, RX GPIO%d, %d bauds", UART_TX_PIN, UART_RX_PIN, UART_BAUD_RATE);
    ESP_LOGI(TAG, "Tasks: uart_rx priority 5, uart_tx priority 4, analyzer priority 3");
    uart_send_line("ESP32 UART console ready");
    uart_send_line("Type HELP for commands");
    return ESP_OK;
}

static void consume_uart_byte(uint8_t byte)
{
    if (byte == '\r' || byte == '\n') {
        if (!s_discard_line && s_line_index > 0) {
            s_line_buffer[s_line_index] = '\0';
            submit_line(s_line_buffer);
        }
        s_line_index = 0;
        s_discard_line = false;
        return;
    }
    if (s_discard_line) {
        return;
    }
    if (byte == '\0' || s_line_index >= UART_BUFFER_SIZE - 1) {
        s_discard_line = true;
        s_line_index = 0;
        uart_send_error("invalid or overlong command");
        return;
    }
    s_line_buffer[s_line_index++] = (char)byte;
}

void uart_console_task(void *arg)
{
    (void)arg;
    uart_event_t event;
    uint8_t bytes[128];
    while (1) {
        if (xQueueReceive(s_uart_events, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining > 0) {
                size_t count = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
                int length = uart_read_bytes(UART_PORT, bytes, count, 0);
                if (length <= 0) {
                    break;
                }
                remaining -= (size_t)length;
                for (int index = 0; index < length; index++) {
                    consume_uart_byte(bytes[index]);
                }
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
                   event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR) {
            uart_flush_input(UART_PORT);
            xQueueReset(s_uart_events);
            s_line_index = 0;
            s_discard_line = true;
            uart_send_error("UART receive fault; resend after newline");
        }
    }
}
