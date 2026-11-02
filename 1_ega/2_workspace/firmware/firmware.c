#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "encoder.h"
#include "vin_calibration.h"
#include "vout_neg_calibration.h"
#include "vout_pos_calibration.h"
#include "settings_storage.h"

// =====================================================
// Configuración
// =====================================================

#define I2C_PORT i2c0
#define SDA_GPIO 12
#define SCL_GPIO 13
#define LCD_ADDR 0x27
#define I2C_TIMEOUT_US 5000U

#define ENCODER1_CLK_GPIO 2
#define ENCODER1_DT_GPIO 3
#define ENCODER1_SW_GPIO 4
#define ENCODER2_CLK_GPIO 6
#define ENCODER2_DT_GPIO 7
#define ENCODER2_SW_GPIO 8
#define ENCODER1_DIRECTION -1
#define ENCODER2_DIRECTION -1

#define UART_ID uart0
#define UART_TX_GPIO 0
#define UART_RX_GPIO 1
#define UART_BAUD_RATE 115200U

#define PWM_GEN_PIN 16
#define PWM_GEN2_PIN 17
#define PWM_FREQUENCY_HZ 50000U

#define ADC_PERIOD_MS 5U
#define PROTECTION_PERIOD_MS 1U
#define CONTROL_PERIOD_MS 5U
#define CONTROL2_PERIOD_MS 5U
#define ENCODER_PERIOD_MS 1U
#define ENCODER_BUTTON_DEBOUNCE_MS 30U
#define ENCODER_LONG_PRESS_MS 800U
#define MEASUREMENT_STALE_MS 150U
#define DISPLAY_UPDATE_MS 100U
#define USB_DIAGNOSTIC_PERIOD_MS 1000U

#define VOLTAGE_SETPOINT_MIN 5.0f
#define VOLTAGE_SETPOINT_MAX 30.0f
#define VOLTAGE_SETPOINT_INITIAL 5.0f
#define VOLTAGE_SETPOINT_STEP 0.5f
#define MAXIMUM_VOLTAGE_REFERENCE 30.0f
#define VOUT_OVERVOLTAGE_TRIP 40.0f
#define OVERCURRENT_TRIP_AMPS 0.50f
#define CURRENT_LIMIT_MIN_AMPS 0.10f
#define CURRENT_LIMIT_MAX_AMPS 0.75f
#define CURRENT_LIMIT_STEP_AMPS 0.05f
#define OVERVOLTAGE_LIMIT_MIN_VOLTS 5.0f
#define OVERVOLTAGE_LIMIT_STEP_VOLTS 0.5f

#define CURRENT_SHUNT_OHMS 0.100f
#define CURRENT_SENSE_GAIN (1.0f + 200000.0f / 22000.0f)
#define CURRENT_POS_ZERO_VOLTS 0.0388f
#define CURRENT2_SHUNT_OHMS 0.100f
#define CURRENT2_SENSE_GAIN (200000.0f / 20000.0f)
#define CURRENT_NEG_ZERO_VOLTS 0.0015f
#define CURRENT_ZERO_DEADBAND_AMPS 0.010f

#define DUTY_MIN_PERCENT 0.0f
#define DUTY_MAX_PERCENT 70.0f

#define SOFT_START_TIME_SECONDS 2.00f

#define PID_KP 0.80f
#define PID_KI 2.00f

#define VIN_ADC_GPIO 26
#define VIN_ADC_CHANNEL 0
#define IOUT2_ADC_GPIO 27
#define IOUT2_ADC_CHANNEL 1
#define IOUT1_ADC_GPIO 28
#define IOUT1_ADC_CHANNEL 2

#define ADC_SAMPLE_DIVIDER 15999.0f

#define VIN_FAST_DROP_VOLTS 2.0f
#define VIN_FAST_DROP_MIN_VOLTS 3.0f

#define ADS1115_ADDR 0x48

#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG 0x01

#define ADS1115_PGA_BITS 0x0200
#define ADS1115_DR_BITS 0x00E0
#define ADS1115_CONV_DELAY_MS 2U

#define ADS_CH_VOUT1 0U
#define ADS_CH_VOUT2 1U

#define ADC_RAW_QUEUE_LENGTH 24U
#define PROTECTION_QUEUE_LENGTH 16U
#define ENCODER_QUEUE_LENGTH 32U
#define FLASH_QUEUE_LENGTH 1U
#define UART_RX_QUEUE_LENGTH 128U
#define CENTRAL_EVENT_QUEUE_LENGTH 80U
#define TASK_STACK_ADC_WORDS 512U
#define TASK_STACK_PROTECTION_WORDS 384U
#define TASK_STACK_CONTROL_WORDS 768U
#define TASK_STACK_CONTROL2_WORDS 768U
#define TASK_STACK_ENCODER_WORDS 512U
#define TASK_STACK_DISPLAY_WORDS 1024U
#define TASK_STACK_UART_WORDS 768U
#define TASK_STACK_ADC_INTERNAL_WORDS 512U
#define TASK_STACK_ADS_WORDS 512U
#define TASK_STACK_FLASH_WORDS 512U

#define TASK_PRIORITY_DISPLAY (tskIDLE_PRIORITY + 1U)
#define TASK_PRIORITY_UART (tskIDLE_PRIORITY + 2U)
#define TASK_PRIORITY_ADC (tskIDLE_PRIORITY + 2U)
#define TASK_PRIORITY_PROTECTION (tskIDLE_PRIORITY + 4U)
#define TASK_PRIORITY_ENCODER (tskIDLE_PRIORITY + 2U)
#define TASK_PRIORITY_CONTROL (tskIDLE_PRIORITY + 3U)
#define TASK_PRIORITY_CONTROL2 (tskIDLE_PRIORITY + 3U)
#define TASK_PRIORITY_ADC_INTERNAL (tskIDLE_PRIORITY + 3U)
#define TASK_PRIORITY_ADS (tskIDLE_PRIORITY + 2U)
#define TASK_PRIORITY_FLASH (tskIDLE_PRIORITY + 1U)

// =====================================================
// Estructuras
// =====================================================

typedef struct {
    uint16_t vin_raw;
    uint16_t vout_raw;
    uint16_t iout_raw;
    uint16_t vout2_raw;
    uint16_t iout2_raw;
    float vin;
    float vout;
    float iout;
    float vout2;
    float iout2;
    bool valid;
    bool valid2;
} power_measurements_t;

typedef struct {
    float voltage_setpoint;
    float voltage_setpoint2;
    bool output_enabled;
    bool output_enabled2;
} control_settings_t;

typedef struct {
    float overcurrent_limit;
    float overcurrent_limit2;
    float overvoltage_limit;
    float overvoltage_limit2;
} runtime_settings_t;

typedef enum {
    EDIT_VOLTAGE_SETPOINT = 0,
    EDIT_CURRENT_LIMIT,
    EDIT_OVERVOLTAGE_LIMIT,
    EDIT_MODE_COUNT
} encoder_edit_mode_t;

typedef struct {
    uint8_t channel;
    uint16_t raw;
} adc_sample_t;

typedef enum {
    MEASUREMENT_INTERNAL = 0,
    MEASUREMENT_VOUT_POSITIVE,
    MEASUREMENT_VOUT_NEGATIVE
} measurement_source_t;

typedef struct {
    measurement_source_t source;
    uint16_t vin_raw;
    uint16_t iout_raw;
    uint16_t iout2_raw;
    float vin;
    float iout;
    float iout2;
    uint16_t vout_raw;
    bool valid;
} measurement_event_t;

typedef struct {
    uint gpio;
    uint32_t events;
    uint32_t levels;
    uint32_t timestamp_ms;
} encoder_event_t;

typedef enum {
    INVALID_COMMAND = 0,
    V,
    IL,
    VL,
    OUT,
    RST,
    GET,
    SAVE
} uart_command_t;

typedef struct {
    uart_command_t comando;
    uint8_t salida;
    float valor;
} uart_event_t;

typedef struct {
    char accion;
    uint8_t salida;
    int direccion;
} encoder_command_t;

typedef enum {
    CENTRAL_EVENT_MEASUREMENT = 0,
    CENTRAL_EVENT_UART_COMMAND,
    CENTRAL_EVENT_ENCODER_COMMAND
} central_event_type_t;

typedef struct {
    central_event_type_t type;
    union {
        measurement_event_t measurement;
        uart_event_t uart_command;
        encoder_command_t encoder_command;
    } data;
} central_event_t;

typedef struct {
    power_measurements_t measurements;
    control_settings_t settings;
    runtime_settings_t limits;
    bool overvoltage_fault;
    bool overvoltage_fault2;
    bool overcurrent_fault;
    bool overcurrent_fault2;
    encoder_edit_mode_t edit_mode;
    encoder_edit_mode_t edit_mode2;
} control_frame_t;

// =====================================================
// Variables globales
// =====================================================

static uint pwm_gen_slice;
static uint pwm_gen_channel;
static uint pwm_gen2_channel;
static uint16_t pwm_gen_wrap;

static power_measurements_t latest_measurements = {0};

static volatile float actual_duty_percent = 0.0f;
static volatile float actual_duty_percent2 = 0.0f;

static volatile bool overvoltage_fault_latched = false;
static volatile bool overvoltage_fault2_latched = false;
static volatile bool overcurrent_fault_latched = false;
static volatile bool overcurrent_fault2_latched = false;
static volatile TickType_t usb_last_ads_tick[2] = {0U, 0U};
static volatile bool usb_ads_seen[2] = {false, false};
static volatile uint16_t adc_latest_raw[3] = {0U, 0U, 0U};
static volatile encoder_edit_mode_t encoder_edit_mode[2] = {EDIT_VOLTAGE_SETPOINT, EDIT_VOLTAGE_SETPOINT};

static float previous_vin = 0.0f;

static control_settings_t control_settings = {.voltage_setpoint = VOLTAGE_SETPOINT_INITIAL, .voltage_setpoint2 = VOLTAGE_SETPOINT_INITIAL, .output_enabled = false, .output_enabled2 = false};

static runtime_settings_t runtime_settings = {.overcurrent_limit = OVERCURRENT_TRIP_AMPS, .overcurrent_limit2 = OVERCURRENT_TRIP_AMPS, .overvoltage_limit = VOUT_OVERVOLTAGE_TRIP, .overvoltage_limit2 = VOUT_OVERVOLTAGE_TRIP};

// =====================================================
// Sincronización FreeRTOS
// =====================================================

static SemaphoreHandle_t i2c_mutex = NULL;
static QueueHandle_t flash_queue = NULL;
static QueueHandle_t adc_raw_queue = NULL;
static QueueHandle_t protection_queue = NULL;
static QueueHandle_t central_event_queue = NULL;
static QueueHandle_t control_positive_queue = NULL;
static QueueHandle_t control_negative_queue = NULL;
static QueueHandle_t display_queue = NULL;
static QueueHandle_t encoder_queue = NULL;
static QueueHandle_t uart_rx_queue = NULL;
static volatile bool uart_rx_discarding = false;
static volatile bool uart_rx_overflow_pending = false;
static volatile bool uart_rx_rearm_requested = false;
static volatile uint8_t adc_channel_index = 0U;

// =====================================================
// Funciones auxiliares
// =====================================================

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void usb_diagnostic_mark_valid(uint8_t output, TickType_t now) {
    taskENTER_CRITICAL();
    usb_last_ads_tick[output] = now;
    usb_ads_seen[output] = true;
    taskEXIT_CRITICAL();
}

static uint32_t usb_diagnostic_age_ms(TickType_t now, TickType_t last, bool seen) {
    if (!seen) {
        return UINT32_MAX;
    }
    return (uint32_t)(now - last) * (uint32_t)portTICK_PERIOD_MS;
}

static void lcd_write_line(uint8_t row, const char *line) {
    uint8_t position;

    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    lcd_set_cursor(row, 0);
    xSemaphoreGive(i2c_mutex);
    for (position = 0U; position < MAX_CHARS && line[position] != '\0'; position++) {
        xSemaphoreTake(i2c_mutex, portMAX_DELAY);
        lcd_char(line[position]);
        xSemaphoreGive(i2c_mutex);
    }
}

static void lcd_show_values(const control_frame_t *frame) {
    const power_measurements_t *measurements = &frame->measurements;
    control_settings_t settings = frame->settings;
    runtime_settings_t limits = frame->limits;
    bool overvoltage_fault = frame->overvoltage_fault;
    bool overvoltage_fault2 = frame->overvoltage_fault2;
    bool overcurrent_fault = frame->overcurrent_fault;
    bool overcurrent_fault2 = frame->overcurrent_fault2;
    encoder_edit_mode_t edit_mode = frame->edit_mode;
    encoder_edit_mode_t edit_mode2 = frame->edit_mode2;

    char line[21];
    char voltage_pos[6];
    char voltage_neg[6];
    char current_pos[6];
    char current_neg[6];
    const char *state_pos = "OF";
    const char *state_neg = "OF";

    if (settings.output_enabled) {
        state_pos = "ON";
    }
    if (settings.output_enabled2) {
        state_neg = "ON";
    }

    snprintf(line, sizeof(line), "Vi:%5.2f -%s +%s    ", measurements->vin, state_neg, state_pos);
    lcd_write_line(0U, line);

    if (overvoltage_fault) {
        snprintf(voltage_pos, sizeof(voltage_pos), "**OV*");
    } else {
        snprintf(voltage_pos, sizeof(voltage_pos), "%5.2f", measurements->vout);
    }
    if (overvoltage_fault2) {
        snprintf(voltage_neg, sizeof(voltage_neg), "**OV*");
    } else {
        snprintf(voltage_neg, sizeof(voltage_neg), "%5.2f", measurements->vout2);
    }
    snprintf(line, sizeof(line), "V-:%s V+:%s   ", voltage_neg, voltage_pos);
    lcd_write_line(1U, line);

    if (overcurrent_fault) {
        snprintf(current_pos, sizeof(current_pos), "**OC*");
    } else {
        snprintf(current_pos, sizeof(current_pos), "%5.2f", measurements->iout);
    }
    if (overcurrent_fault2) {
        snprintf(current_neg, sizeof(current_neg), "**OC*");
    } else {
        snprintf(current_neg, sizeof(current_neg), "%5.2f", measurements->iout2);
    }
    snprintf(line, sizeof(line), "I-:%s I+:%s   ", current_neg, current_pos);
    lcd_write_line(2U, line);

    char edit_pos[10];
    char edit_neg[10];
    if (edit_mode == EDIT_CURRENT_LIMIT) {
        snprintf(edit_pos, sizeof(edit_pos), "IL+:%4.2f", limits.overcurrent_limit);
    } else if (edit_mode == EDIT_OVERVOLTAGE_LIMIT) {
        snprintf(edit_pos, sizeof(edit_pos), "OV+:%4.1f", limits.overvoltage_limit);
    } else {
        snprintf(edit_pos, sizeof(edit_pos), "V+:%4.1f", settings.voltage_setpoint);
    }
    if (edit_mode2 == EDIT_CURRENT_LIMIT) {
        snprintf(edit_neg, sizeof(edit_neg), "IL-:%4.2f", limits.overcurrent_limit2);
    } else if (edit_mode2 == EDIT_OVERVOLTAGE_LIMIT) {
        snprintf(edit_neg, sizeof(edit_neg), "OV-:%4.1f", limits.overvoltage_limit2);
    } else {
        snprintf(edit_neg, sizeof(edit_neg), "V-:%4.1f", settings.voltage_setpoint2);
    }
    snprintf(line, sizeof(line), "%-9s%-9s  ", edit_neg, edit_pos);
    lcd_write_line(3U, line);
}

// =====================================================
// Medición y protección
// =====================================================

static uint16_t adc_read_channel(uint8_t channel) {
    adc_select_input(channel);
    return adc_read();
}

static bool adc_fifo_recover_overflow(void) {
    const uint32_t round_robin_mask = (1U << VIN_ADC_CHANNEL) | (1U << IOUT2_ADC_CHANNEL) | (1U << IOUT1_ADC_CHANNEL);
    if ((adc_hw->fcs & ADC_FCS_OVER_BITS) == 0U) {
        return false;
    }

    adc_run(false);
    adc_fifo_drain();
    adc_hw->fcs |= ADC_FCS_OVER_BITS;
    adc_set_round_robin(0U);
    adc_select_input(VIN_ADC_CHANNEL);
    adc_set_round_robin(round_robin_mask);
    adc_channel_index = 0U;
    adc_run(true);
    return true;
}

static void adc_fifo_irq_handler(void) {
    static const uint8_t channels[3] = {VIN_ADC_CHANNEL, IOUT2_ADC_CHANNEL, IOUT1_ADC_CHANNEL};
    BaseType_t higher_priority_woken = pdFALSE;

    if (adc_fifo_recover_overflow()) {
        return;
    }

    while (!adc_fifo_is_empty()) {
        adc_sample_t sample;
        uint16_t raw = adc_fifo_get();
        if (adc_fifo_recover_overflow()) {
            break;
        }
        sample.channel = channels[adc_channel_index];
        sample.raw = raw;
        adc_channel_index++;
        if (adc_channel_index >= 3U) {
            adc_channel_index = 0U;
        }
        xQueueSendFromISR(adc_raw_queue, &sample, &higher_priority_woken);
        if (sample.channel == IOUT1_ADC_CHANNEL || sample.channel == IOUT2_ADC_CHANNEL) {
            xQueueSendFromISR(protection_queue, &sample, &higher_priority_woken);
        }
    }
    portYIELD_FROM_ISR(higher_priority_woken);
}

static void init_adc_interrupt(void) {
    adc_init();
    adc_gpio_init(VIN_ADC_GPIO);
    adc_gpio_init(IOUT2_ADC_GPIO);
    adc_gpio_init(IOUT1_ADC_GPIO);
    adc_run(false);
    adc_set_round_robin(0U);
    adc_fifo_drain();
    adc_select_input(VIN_ADC_CHANNEL);
    adc_set_round_robin((1U << VIN_ADC_CHANNEL) | (1U << IOUT2_ADC_CHANNEL) | (1U << IOUT1_ADC_CHANNEL));
    adc_set_clkdiv(ADC_SAMPLE_DIVIDER);
    adc_channel_index = 0U;
    adc_fifo_setup(true, false, 1U, false, false);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_fifo_irq_handler);
    irq_set_priority(ADC_IRQ_FIFO, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    adc_irq_set_enabled(true);
    irq_set_enabled(ADC_IRQ_FIFO, true);
}

static void encoder_gpio_irq_handler(uint gpio, uint32_t events) {
    encoder_event_t event;
    BaseType_t higher_priority_woken = pdFALSE;
    event.gpio = gpio;
    event.events = events;
    event.levels = gpio_get_all();
    event.timestamp_ms = to_ms_since_boot(get_absolute_time());
    xQueueSendFromISR(encoder_queue, &event, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

static void init_encoder_interrupts(void) {
    const uint encoder_pins[] = {ENCODER1_CLK_GPIO, ENCODER1_DT_GPIO, ENCODER1_SW_GPIO, ENCODER2_CLK_GPIO, ENCODER2_DT_GPIO, ENCODER2_SW_GPIO};
    for (uint index = 0U; index < 6U; index++) {
        gpio_init(encoder_pins[index]);
        gpio_set_dir(encoder_pins[index], GPIO_IN);
        gpio_pull_up(encoder_pins[index]);
    }
    irq_set_priority(IO_IRQ_BANK0, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    gpio_set_irq_enabled_with_callback(ENCODER1_CLK_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, encoder_gpio_irq_handler);
    gpio_set_irq_enabled(ENCODER1_DT_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENCODER1_SW_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENCODER2_CLK_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENCODER2_DT_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENCODER2_SW_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

static bool ads1115_read_channel(uint8_t channel, uint16_t *raw_value) {
    uint16_t config = 0x8000U;
    config |= (uint16_t)((0x04U | channel) << 12);
    config |= ADS1115_PGA_BITS | 0x0100U | ADS1115_DR_BITS | 0x0003U;
    uint8_t config_data[3] = {ADS1115_REG_CONFIG, (uint8_t)(config >> 8), (uint8_t)config};
    uint8_t register_address = ADS1115_REG_CONVERSION;
    uint8_t received[2] = {0U, 0U};

    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    int written = i2c_write_timeout_us(I2C_PORT, ADS1115_ADDR, config_data, 3, false, I2C_TIMEOUT_US);
    xSemaphoreGive(i2c_mutex);
    if (written != 3) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(ADS1115_CONV_DELAY_MS) + 1U);
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    written = i2c_write_timeout_us(I2C_PORT, ADS1115_ADDR, &register_address, 1, true, I2C_TIMEOUT_US);
    int read = -1;
    if (written == 1) {
        read = i2c_read_timeout_us(I2C_PORT, ADS1115_ADDR, received, 2, false, I2C_TIMEOUT_US);
    }
    xSemaphoreGive(i2c_mutex);
    if (written != 1 || read != 2) {
        return false;
    }

    int16_t signed_raw = (int16_t)(((uint16_t)received[0] << 8) | received[1]);
    *raw_value = 0U;
    if (signed_raw > 0) {
        *raw_value = (uint16_t)signed_raw;
    }
    return true;
}

static void read_measurements(power_measurements_t *measurements) {
    runtime_settings_t limits;

    taskENTER_CRITICAL();
    measurements->vin_raw = adc_latest_raw[VIN_ADC_CHANNEL];
    measurements->iout_raw = adc_latest_raw[IOUT1_ADC_CHANNEL];
    measurements->iout2_raw = adc_latest_raw[IOUT2_ADC_CHANNEL];
    limits = runtime_settings;
    taskEXIT_CRITICAL();

    measurements->vin = vin_from_adc_raw(measurements->vin_raw);
    measurements->iout = (((float)measurements->iout_raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_POS_ZERO_VOLTS) / (CURRENT_SHUNT_OHMS * CURRENT_SENSE_GAIN);
    measurements->iout2 = (((float)measurements->iout2_raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_NEG_ZERO_VOLTS) / (CURRENT2_SHUNT_OHMS * CURRENT2_SENSE_GAIN);
    if (measurements->iout < CURRENT_ZERO_DEADBAND_AMPS) {
        measurements->iout = 0.0f;
    }
    if (measurements->iout2 < CURRENT_ZERO_DEADBAND_AMPS) {
        measurements->iout2 = 0.0f;
    }

    measurements->valid = false;
    measurements->valid2 = false;
    for (uint8_t channel = ADS_CH_VOUT1; channel <= ADS_CH_VOUT2; channel++) {
        uint16_t config = 0x8000U;
        config |= (uint16_t)((0x04U | channel) << 12);
        config |= ADS1115_PGA_BITS | 0x0100U | ADS1115_DR_BITS | 0x0003U;
        uint8_t config_data[3] = {ADS1115_REG_CONFIG, (uint8_t)(config >> 8), (uint8_t)config};

        xSemaphoreTake(i2c_mutex, portMAX_DELAY);
        int written = i2c_write_timeout_us(I2C_PORT, ADS1115_ADDR, config_data, 3, false, I2C_TIMEOUT_US);
        xSemaphoreGive(i2c_mutex);
        if (written != 3) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(ADS1115_CONV_DELAY_MS));
        uint8_t register_address = ADS1115_REG_CONVERSION;
        uint8_t received[2] = {0U, 0U};
        xSemaphoreTake(i2c_mutex, portMAX_DELAY);
        written = i2c_write_timeout_us(I2C_PORT, ADS1115_ADDR, &register_address, 1, true, I2C_TIMEOUT_US);
        int read = -1;
        if (written == 1) {
            read = i2c_read_timeout_us(I2C_PORT, ADS1115_ADDR, received, 2, false, I2C_TIMEOUT_US);
        }
        xSemaphoreGive(i2c_mutex);
        if (written != 1 || read != 2) {
            continue;
        }

        int16_t signed_raw = (int16_t)(((uint16_t)received[0] << 8) | received[1]);
        uint16_t raw = 0U;
        if (signed_raw > 0) {
            raw = (uint16_t)signed_raw;
        }

        if (channel == ADS_CH_VOUT1) {
            measurements->vout_raw = raw;
            measurements->vout = vout_pos_from_ads_raw(raw);
            measurements->valid = true;
            if (measurements->vout > limits.overvoltage_limit) {
                taskENTER_CRITICAL();
                overvoltage_fault_latched = true;
                control_settings.output_enabled = false;
                pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
                actual_duty_percent = 0.0f;
                taskEXIT_CRITICAL();
            }
        } else {
            measurements->vout2_raw = raw;
            measurements->vout2 = vout_neg_from_ads_raw(raw);
            measurements->valid2 = true;
            if (measurements->vout2 > limits.overvoltage_limit2) {
                taskENTER_CRITICAL();
                overvoltage_fault2_latched = true;
                control_settings.output_enabled2 = false;
                pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
                actual_duty_percent2 = 0.0f;
                taskEXIT_CRITICAL();
            }
        }
    }
}

// =====================================================
// Control PWM
// =====================================================

static void pwm_set_duty_safe(float requested_duty) {
    float safe_duty = clamp_float(requested_duty, DUTY_MIN_PERCENT, DUTY_MAX_PERCENT);
    uint32_t level = (uint32_t)(((float)(pwm_gen_wrap + 1U) * safe_duty) / 100.0f);
    taskENTER_CRITICAL();
    if (overcurrent_fault_latched || overvoltage_fault_latched || !control_settings.output_enabled) {
        safe_duty = 0.0f;
        level = 0U;
    }
    pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, (uint16_t)level);
    actual_duty_percent = safe_duty;
    taskEXIT_CRITICAL();
}

static void pwm_set_duty2_safe(float requested_duty) {
    float safe_duty = clamp_float(requested_duty, DUTY_MIN_PERCENT, DUTY_MAX_PERCENT);
    uint32_t level = (uint32_t)(((float)(pwm_gen_wrap + 1U) * safe_duty) / 100.0f);
    taskENTER_CRITICAL();
    if (overcurrent_fault2_latched || overvoltage_fault2_latched || !control_settings.output_enabled2) {
        safe_duty = 0.0f;
        level = 0U;
    }
    pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, (uint16_t)level);
    actual_duty_percent2 = safe_duty;
    taskEXIT_CRITICAL();
}

static bool pwm_generator_init(uint gpio, uint32_t frequency_hz) {
    if (frequency_hz == 0U) {
        return false;
    }
    if (pwm_gpio_to_slice_num(gpio) != pwm_gpio_to_slice_num(PWM_GEN2_PIN)) {
        return false;
    }
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    gpio_set_function(PWM_GEN2_PIN, GPIO_FUNC_PWM);
    pwm_gen_slice = pwm_gpio_to_slice_num(gpio);
    pwm_gen_channel = pwm_gpio_to_channel(gpio);
    pwm_gen2_channel = pwm_gpio_to_channel(PWM_GEN2_PIN);
    uint32_t clock_hz = clock_get_hz(clk_sys);
    uint64_t denominator = (uint64_t)frequency_hz * 65536ULL;
    uint32_t div16 = (uint32_t)(((uint64_t)clock_hz * 16ULL + denominator - 1ULL) / denominator);
    if (div16 < 16U) {
        div16 = 16U;
    }
    if (div16 > 4095U) {
        return false;
    }
    uint32_t calculated_wrap = (uint32_t)(((uint64_t)clock_hz * 16ULL) / ((uint64_t)div16 * frequency_hz));
    if (calculated_wrap == 0U) {
        return false;
    }
    calculated_wrap--;
    if (calculated_wrap > 65535U) {
        return false;
    }
    pwm_gen_wrap = (uint16_t)calculated_wrap;
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&config, (uint8_t)(div16 / 16U), (uint8_t)(div16 % 16U));
    pwm_config_set_wrap(&config, pwm_gen_wrap);
    pwm_init(pwm_gen_slice, &config, false);
    pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
    pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
    pwm_set_enabled(pwm_gen_slice, true);
    actual_duty_percent = 0.0f;
    actual_duty_percent2 = 0.0f;
    return true;
}

// =====================================================
// Funciones de control y procesamiento de comandos
// =====================================================

static void central_apply_encoder_command(const encoder_command_t *command) {
    if (command == NULL || command->salida > 1U) {
        return;
    }

    taskENTER_CRITICAL();
    switch (command->accion) {
        case 'G':
            if (command->salida == 0U) {
                if (encoder_edit_mode[0] == EDIT_CURRENT_LIMIT) {
                    runtime_settings.overcurrent_limit = clamp_float(runtime_settings.overcurrent_limit + (float)command->direccion * CURRENT_LIMIT_STEP_AMPS, CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
                } else if (encoder_edit_mode[0] == EDIT_OVERVOLTAGE_LIMIT) {
                    runtime_settings.overvoltage_limit = clamp_float(runtime_settings.overvoltage_limit + (float)command->direccion * OVERVOLTAGE_LIMIT_STEP_VOLTS, OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
                    control_settings.voltage_setpoint = clamp_float(control_settings.voltage_setpoint, VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit);
                } else {
                    control_settings.voltage_setpoint = clamp_float(control_settings.voltage_setpoint + (float)command->direccion * VOLTAGE_SETPOINT_STEP, VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit);
                }
            } else if (encoder_edit_mode[1] == EDIT_CURRENT_LIMIT) {
                runtime_settings.overcurrent_limit2 = clamp_float(runtime_settings.overcurrent_limit2 + (float)command->direccion * CURRENT_LIMIT_STEP_AMPS, CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
            } else if (encoder_edit_mode[1] == EDIT_OVERVOLTAGE_LIMIT) {
                runtime_settings.overvoltage_limit2 = clamp_float(runtime_settings.overvoltage_limit2 + (float)command->direccion * OVERVOLTAGE_LIMIT_STEP_VOLTS, OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
                control_settings.voltage_setpoint2 = clamp_float(control_settings.voltage_setpoint2, VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit2);
            } else {
                control_settings.voltage_setpoint2 = clamp_float(control_settings.voltage_setpoint2 + (float)command->direccion * VOLTAGE_SETPOINT_STEP, VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit2);
            }
            break;

        case 'C':
            encoder_edit_mode[command->salida] = (encoder_edit_mode_t)((encoder_edit_mode[command->salida] + 1U) % EDIT_MODE_COUNT);
            break;

        case 'L':
            if (command->salida == 0U) {
                if (overcurrent_fault_latched || overvoltage_fault_latched) {
                    overcurrent_fault_latched = false;
                    overvoltage_fault_latched = false;
                    control_settings.output_enabled = false;
                } else if (control_settings.output_enabled) {
                    control_settings.output_enabled = false;
                } else {
                    control_settings.output_enabled = true;
                }
            } else if (overcurrent_fault2_latched || overvoltage_fault2_latched) {
                overcurrent_fault2_latched = false;
                overvoltage_fault2_latched = false;
                control_settings.output_enabled2 = false;
            } else if (control_settings.output_enabled2) {
                control_settings.output_enabled2 = false;
            } else {
                control_settings.output_enabled2 = true;
            }
            break;

        default:
            break;
    }
    taskEXIT_CRITICAL();
}

static void central_apply_uart_command(const uart_event_t *command) {
    bool disable_positive = false;
    bool disable_negative = false;
    bool queue_flash = false;
    bool enabled = false;
    bool fault = false;
    stored_settings_t flash_values = {0};

    if (command == NULL || command->salida > 1U) {
        return;
    }

    taskENTER_CRITICAL();
    switch (command->comando) {
        case V:
            if (command->salida == 0U && command->valor <= runtime_settings.overvoltage_limit) {
                control_settings.voltage_setpoint = command->valor;
            } else if (command->salida == 1U && command->valor <= runtime_settings.overvoltage_limit2) {
                control_settings.voltage_setpoint2 = command->valor;
            }
            break;

        case IL:
            if (command->salida == 0U && !control_settings.output_enabled) {
                runtime_settings.overcurrent_limit = command->valor;
            } else if (command->salida == 1U && !control_settings.output_enabled2) {
                runtime_settings.overcurrent_limit2 = command->valor;
            }
            break;

        case VL:
            if (command->salida == 0U && !control_settings.output_enabled) {
                runtime_settings.overvoltage_limit = command->valor;
                control_settings.voltage_setpoint = clamp_float(control_settings.voltage_setpoint, VOLTAGE_SETPOINT_MIN, command->valor);
            } else if (command->salida == 1U && !control_settings.output_enabled2) {
                runtime_settings.overvoltage_limit2 = command->valor;
                control_settings.voltage_setpoint2 = clamp_float(control_settings.voltage_setpoint2, VOLTAGE_SETPOINT_MIN, command->valor);
            }
            break;

        case RST:
            if (command->salida == 0U) {
                control_settings.output_enabled = false;
                overcurrent_fault_latched = false;
                overvoltage_fault_latched = false;
                disable_positive = true;
            } else {
                control_settings.output_enabled2 = false;
                overcurrent_fault2_latched = false;
                overvoltage_fault2_latched = false;
                disable_negative = true;
            }
            break;

        case OUT:
            enabled = command->valor != 0.0f;
            if (command->salida == 0U) {
                fault = overcurrent_fault_latched || overvoltage_fault_latched;
                if (!enabled) {
                    control_settings.output_enabled = false;
                    disable_positive = true;
                } else if (!fault) {
                    control_settings.output_enabled = true;
                }
            } else {
                fault = overcurrent_fault2_latched || overvoltage_fault2_latched;
                if (!enabled) {
                    control_settings.output_enabled2 = false;
                    disable_negative = true;
                } else if (!fault) {
                    control_settings.output_enabled2 = true;
                }
            }
            break;

        case SAVE:
            if (!control_settings.output_enabled && !control_settings.output_enabled2) {
                flash_values.voltage_positive = control_settings.voltage_setpoint;
                flash_values.voltage_negative = control_settings.voltage_setpoint2;
                flash_values.current_positive = runtime_settings.overcurrent_limit;
                flash_values.current_negative = runtime_settings.overcurrent_limit2;
                flash_values.overvoltage_positive = runtime_settings.overvoltage_limit;
                flash_values.overvoltage_negative = runtime_settings.overvoltage_limit2;
                queue_flash = true;
            }
            break;

        case GET:
        case INVALID_COMMAND:
        default:
            break;
    }
    taskEXIT_CRITICAL();

    if (disable_positive) {
        pwm_set_duty_safe(0.0f);
    }
    if (disable_negative) {
        pwm_set_duty2_safe(0.0f);
    }
    if (queue_flash) {
        xQueueSend(flash_queue, &flash_values, 0U);
    }
}

// =====================================================
// Tareas FreeRTOS
// =====================================================

static void protection_task(void *parameters) {
    (void)parameters;
    adc_sample_t sample;
    while (true) {
        xQueueReceive(protection_queue, &sample, portMAX_DELAY);
        taskENTER_CRITICAL();
        if (sample.channel == IOUT1_ADC_CHANNEL) {
            float current_positive = (((float)sample.raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_POS_ZERO_VOLTS) / (CURRENT_SHUNT_OHMS * CURRENT_SENSE_GAIN);
            if (control_settings.output_enabled && current_positive >= runtime_settings.overcurrent_limit) {
                overcurrent_fault_latched = true;
                control_settings.output_enabled = false;
                pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
                actual_duty_percent = 0.0f;
            }
        } else if (sample.channel == IOUT2_ADC_CHANNEL) {
            float current_negative = (((float)sample.raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_NEG_ZERO_VOLTS) / (CURRENT2_SHUNT_OHMS * CURRENT2_SENSE_GAIN);
            if (control_settings.output_enabled2 && current_negative >= runtime_settings.overcurrent_limit2) {
                overcurrent_fault2_latched = true;
                control_settings.output_enabled2 = false;
                pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
                actual_duty_percent2 = 0.0f;
            }
        }
        taskEXIT_CRITICAL();
    }
}

static void adc_internal_task(void *parameters) {
    (void)parameters;
    adc_sample_t sample;
    central_event_t central_event = {0};
    measurement_event_t *event = &central_event.data.measurement;
    bool have_vin = false;
    bool have_positive = false;
    bool have_negative = false;

    adc_run(true);

    while (true) {
        xQueueReceive(adc_raw_queue, &sample, portMAX_DELAY);
        taskENTER_CRITICAL();
        adc_latest_raw[sample.channel] = sample.raw;
        if (sample.channel == VIN_ADC_CHANNEL) {
            have_vin = true;
        } else if (sample.channel == IOUT1_ADC_CHANNEL) {
            have_positive = true;
        } else if (sample.channel == IOUT2_ADC_CHANNEL) {
            have_negative = true;
        }
        taskEXIT_CRITICAL();

        if (sample.channel == IOUT1_ADC_CHANNEL && have_vin && have_positive && have_negative) {
            central_event.type = CENTRAL_EVENT_MEASUREMENT;
            taskENTER_CRITICAL();
            event->source = MEASUREMENT_INTERNAL;
            event->vin_raw = adc_latest_raw[VIN_ADC_CHANNEL];
            event->iout_raw = adc_latest_raw[IOUT1_ADC_CHANNEL];
            event->iout2_raw = adc_latest_raw[IOUT2_ADC_CHANNEL];
            taskEXIT_CRITICAL();
            event->vin = vin_from_adc_raw(event->vin_raw);
            event->iout = (((float)event->iout_raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_POS_ZERO_VOLTS) / (CURRENT_SHUNT_OHMS * CURRENT_SENSE_GAIN);
            event->iout2 = (((float)event->iout2_raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_NEG_ZERO_VOLTS) / (CURRENT2_SHUNT_OHMS * CURRENT2_SENSE_GAIN);
            if (event->iout < CURRENT_ZERO_DEADBAND_AMPS) {
                event->iout = 0.0f;
            }
            if (event->iout2 < CURRENT_ZERO_DEADBAND_AMPS) {
                event->iout2 = 0.0f;
            }
            xQueueSend(central_event_queue, &central_event, 0U);
        }
    }
}

static void ads1115_task(void *parameters) {
    (void)parameters;
    central_event_t central_event = {0};
    measurement_event_t *event = &central_event.data.measurement;
    uint16_t raw;

    central_event.type = CENTRAL_EVENT_MEASUREMENT;
    while (true) {
        if (ads1115_read_channel(ADS_CH_VOUT1, &raw)) {
            event->source = MEASUREMENT_VOUT_POSITIVE;
            event->vout_raw = raw;
            event->valid = true;
            xQueueSend(central_event_queue, &central_event, portMAX_DELAY);
        }
        if (ads1115_read_channel(ADS_CH_VOUT2, &raw)) {
            event->source = MEASUREMENT_VOUT_NEGATIVE;
            event->vout_raw = raw;
            event->valid = true;
            xQueueSend(central_event_queue, &central_event, portMAX_DELAY);
        }
    }
}

static void encoder_task(void *parameters) {
    (void)parameters;
    encoder_t encoders[2];
    encoder_event_t event;
    encoder_command_t command;
    central_event_t central_event = {0};
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    encoder_init(&encoders[0], ENCODER1_CLK_GPIO, ENCODER1_DT_GPIO, ENCODER1_SW_GPIO, ENCODER_BUTTON_DEBOUNCE_MS, ENCODER_LONG_PRESS_MS, now_ms);
    encoder_init(&encoders[1], ENCODER2_CLK_GPIO, ENCODER2_DT_GPIO, ENCODER2_SW_GPIO, ENCODER_BUTTON_DEBOUNCE_MS, ENCODER_LONG_PRESS_MS, now_ms);
    while (true) {
        xQueueReceive(encoder_queue, &event, portMAX_DELAY);
        now_ms = event.timestamp_ms;
        for (uint8_t branch = 0U; branch < 2U; branch++) {
            int movement = 0;
            encoder_button_event_t button_event = ENCODER_BUTTON_EVENT_NONE;
            bool handled = encoder_process_event(&encoders[branch], event.gpio, event.levels, now_ms, &movement, &button_event);
            if (!handled) {
                continue;
            }
            if (movement != 0) {
                int direction = ENCODER1_DIRECTION;
                if (branch != 0U) {
                    direction = ENCODER2_DIRECTION;
                }
                command = (encoder_command_t){'G', branch, movement * direction};
                central_event.type = CENTRAL_EVENT_ENCODER_COMMAND;
                central_event.data.encoder_command = command;
                xQueueSend(central_event_queue, &central_event, portMAX_DELAY);
            }
            if (button_event != ENCODER_BUTTON_EVENT_NONE) {
                command.salida = branch;
                command.direccion = 0;
                if (button_event == ENCODER_BUTTON_EVENT_SHORT) {
                    command.accion = 'C';
                } else {
                    command.accion = 'L';
                }
                central_event.type = CENTRAL_EVENT_ENCODER_COMMAND;
                central_event.data.encoder_command = command;
                xQueueSend(central_event_queue, &central_event, portMAX_DELAY);
            }
            break;
        }
    }
}

static void central_task(void *parameters) {
    (void)parameters;
    power_measurements_t measurements = {0};
    central_event_t central_event;
    measurement_event_t *measurement_event;
    control_frame_t frame;
    UBaseType_t event_index;
    bool have_internal = false;
    bool have_positive = false;
    bool have_negative = false;
    bool publish;
    TickType_t now;
    TickType_t wait_ticks;
    TickType_t last_positive = xTaskGetTickCount();
    TickType_t last_negative = last_positive;
    const TickType_t stale_ticks = pdMS_TO_TICKS(MEASUREMENT_STALE_MS);
    float voltage;

    while (true) {
        publish = false;
        for (event_index = 0U; event_index < CENTRAL_EVENT_QUEUE_LENGTH; event_index++) {
            wait_ticks = event_index == 0U ? pdMS_TO_TICKS(5U) : 0U;
            if (xQueueReceive(central_event_queue, &central_event, wait_ticks) != pdPASS) {
                break;
            }
            switch (central_event.type) {
                case CENTRAL_EVENT_UART_COMMAND:
                    central_apply_uart_command(&central_event.data.uart_command);
                    publish = true;
                    break;

                case CENTRAL_EVENT_ENCODER_COMMAND:
                    central_apply_encoder_command(&central_event.data.encoder_command);
                    publish = true;
                    break;

                case CENTRAL_EVENT_MEASUREMENT:
                    measurement_event = &central_event.data.measurement;
                    now = xTaskGetTickCount();
                    if (measurement_event->source == MEASUREMENT_INTERNAL) {
                        measurements.vin_raw = measurement_event->vin_raw;
                        measurements.iout_raw = measurement_event->iout_raw;
                        measurements.iout2_raw = measurement_event->iout2_raw;
                        if (have_internal) {
                            measurements.vin = (measurements.vin + measurement_event->vin) / 2.0f;
                            measurements.iout = (measurements.iout + measurement_event->iout) / 2.0f;
                            measurements.iout2 = (measurements.iout2 + measurement_event->iout2) / 2.0f;
                        } else {
                            measurements.vin = measurement_event->vin;
                            measurements.iout = measurement_event->iout;
                            measurements.iout2 = measurement_event->iout2;
                            have_internal = true;
                        }
                        publish = true;
                    } else if (measurement_event->source == MEASUREMENT_VOUT_POSITIVE) {
                        voltage = vout_pos_from_ads_raw(measurement_event->vout_raw);
                        measurements.vout_raw = measurement_event->vout_raw;
                        if (have_positive) {
                            measurements.vout = (measurements.vout + voltage) / 2.0f;
                        } else {
                            measurements.vout = voltage;
                            have_positive = true;
                        }
                        measurements.valid = measurement_event->valid;
                        last_positive = now;
                        if (measurement_event->valid) {
                            usb_diagnostic_mark_valid(0U, now);
                        }
                        taskENTER_CRITICAL();
                        if (measurements.vout > runtime_settings.overvoltage_limit) {
                            overvoltage_fault_latched = true;
                            control_settings.output_enabled = false;
                            pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
                            actual_duty_percent = 0.0f;
                        }
                        taskEXIT_CRITICAL();
                        publish = true;
                    } else {
                        voltage = vout_neg_from_ads_raw(measurement_event->vout_raw);
                        measurements.vout2_raw = measurement_event->vout_raw;
                        if (have_negative) {
                            measurements.vout2 = (measurements.vout2 + voltage) / 2.0f;
                        } else {
                            measurements.vout2 = voltage;
                            have_negative = true;
                        }
                        measurements.valid2 = measurement_event->valid;
                        last_negative = now;
                        if (measurement_event->valid) {
                            usb_diagnostic_mark_valid(1U, now);
                        }
                        taskENTER_CRITICAL();
                        if (measurements.vout2 > runtime_settings.overvoltage_limit2) {
                            overvoltage_fault2_latched = true;
                            control_settings.output_enabled2 = false;
                            pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
                            actual_duty_percent2 = 0.0f;
                        }
                        taskEXIT_CRITICAL();
                        publish = true;
                    }
                    break;

                default:
                    break;
            }
        }

        now = xTaskGetTickCount();
        if (have_positive && (now - last_positive) >= stale_ticks) {
            measurements.valid = false;
            publish = true;
        }
        if (have_negative && (now - last_negative) >= stale_ticks) {
            measurements.valid2 = false;
            publish = true;
        }
        if (publish) {
            taskENTER_CRITICAL();
            latest_measurements = measurements;
            frame.measurements = measurements;
            frame.settings = control_settings;
            frame.limits = runtime_settings;
            frame.overvoltage_fault = overvoltage_fault_latched;
            frame.overvoltage_fault2 = overvoltage_fault2_latched;
            frame.overcurrent_fault = overcurrent_fault_latched;
            frame.overcurrent_fault2 = overcurrent_fault2_latched;
            frame.edit_mode = encoder_edit_mode[0];
            frame.edit_mode2 = encoder_edit_mode[1];
            taskEXIT_CRITICAL();
            xQueueOverwrite(control_positive_queue, &frame);
            xQueueOverwrite(control_negative_queue, &frame);
            xQueueOverwrite(display_queue, &frame);
        }
    }
}

static void control_positive_task(void *parameters) {
    (void)parameters;
    control_frame_t queued_frame = {0};
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
    const float dt = (float)CONTROL_PERIOD_MS / 1000.0f;
    uint64_t soft_start_us = time_us_64();
    bool was_enabled = false;
    float integral = 0.0f;

    while (true) {
        uint64_t now_us = time_us_64();
        while (xQueueReceive(control_positive_queue, &queued_frame, 0U) == pdPASS) {
        }

        bool ready = queued_frame.measurements.valid;
        bool enabled = queued_frame.settings.output_enabled;
        bool overvoltage = queued_frame.overvoltage_fault;
        bool overcurrent = queued_frame.overcurrent_fault;
        float vout = queued_frame.measurements.vout;
        float target = queued_frame.settings.voltage_setpoint;
        if (!ready || !enabled || overcurrent || overvoltage) {
            pwm_set_duty_safe(0.0f);
            integral = 0.0f;
            was_enabled = false;
            vTaskDelayUntil(&last_wake, period);
            continue;
        }

        if (!was_enabled) {
            soft_start_us = now_us;
            was_enabled = true;
        }

        float ramp = clamp_float((float)(now_us - soft_start_us) / (1000000.0f * SOFT_START_TIME_SECONDS), 0.0f, 1.0f);
        target = clamp_float(target, VOLTAGE_SETPOINT_MIN, MAXIMUM_VOLTAGE_REFERENCE);
        float reference = ramp * target;
        float error = reference - vout;
        integral = clamp_float(integral + error * dt, 0.0f, DUTY_MAX_PERCENT / PID_KI);
        float duty = clamp_float(PID_KP * error + PID_KI * integral, DUTY_MIN_PERCENT, DUTY_MAX_PERCENT);
        pwm_set_duty_safe(duty);
        vTaskDelayUntil(&last_wake, period);
    }
}

static void control_negative_task(void *parameters) {
    (void)parameters;
    control_frame_t queued_frame = {0};
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONTROL2_PERIOD_MS);
    const float dt = (float)CONTROL2_PERIOD_MS / 1000.0f;
    uint64_t soft_start_us = time_us_64();
    bool was_enabled = false;
    float integral = 0.0f;

    while (true) {
        uint64_t now_us = time_us_64();
        while (xQueueReceive(control_negative_queue, &queued_frame, 0U) == pdPASS) {
        }

        bool ready = queued_frame.measurements.valid2;
        bool enabled = queued_frame.settings.output_enabled2;
        bool overvoltage = queued_frame.overvoltage_fault2;
        bool overcurrent = queued_frame.overcurrent_fault2;
        float vout = queued_frame.measurements.vout2;
        float target = queued_frame.settings.voltage_setpoint2;
        if (!ready || !enabled || overcurrent || overvoltage) {
            pwm_set_duty2_safe(0.0f);
            integral = 0.0f;
            was_enabled = false;
            vTaskDelayUntil(&last_wake, period);
            continue;
        }

        if (!was_enabled) {
            soft_start_us = now_us;
            was_enabled = true;
        }

        float ramp = clamp_float((float)(now_us - soft_start_us) / (1000000.0f * SOFT_START_TIME_SECONDS), 0.0f, 1.0f);
        target = clamp_float(target, VOLTAGE_SETPOINT_MIN, MAXIMUM_VOLTAGE_REFERENCE);
        float reference = ramp * target;
        float error = reference - vout;
        integral = clamp_float(integral + error * dt, 0.0f, DUTY_MAX_PERCENT / PID_KI);
        float duty = clamp_float(PID_KP * error + PID_KI * integral, DUTY_MIN_PERCENT, DUTY_MAX_PERCENT);
        pwm_set_duty2_safe(duty);
        vTaskDelayUntil(&last_wake, period);
    }
}

static void flash_task(void *parameters) {
    (void)parameters;
    while (true) {
        stored_settings_t values;
        if (xQueueReceive(flash_queue, &values, portMAX_DELAY) == pdPASS) {
            settings_storage_save(&values);
        }
    }
}

static void uart_rx_isr(void) {
    BaseType_t higher_priority_woken = pdFALSE;
    uint8_t received;

    while (uart_is_readable(UART_ID)) {
        received = (uint8_t)uart_getc(UART_ID);
        if (uart_rx_discarding) {
            if (received == '\n' && uart_rx_rearm_requested) {
                uart_rx_discarding = false;
                uart_rx_rearm_requested = false;
            }
            continue;
        }
        if (xQueueSendFromISR(uart_rx_queue, &received, &higher_priority_woken) != pdPASS) {
            uart_rx_discarding = true;
            uart_rx_overflow_pending = true;
        }
    }
    portYIELD_FROM_ISR(higher_priority_woken);
}

static void uart_task(void *parameters) {
    char command[48];
    char response[320];
    char name[6];
    char state[48];
    uint8_t received;
    int output;
    float value;
    uart_command_t comando;
    uart_event_t uart_event;
    central_event_t central_event = {0};
    size_t length = 0U;
    bool overflow = false;
    power_measurements_t measurements;
    control_settings_t settings;
    runtime_settings_t limits;
    bool overcurrent_fault;
    bool overcurrent_fault2;
    bool overvoltage_fault;
    bool overvoltage_fault2;
    bool ready_positive;
    bool ready_negative;
    float duty_positive;
    float duty_negative;
    bool rx_overflow_pending;
    const uint uart_irq = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;

    (void)parameters;

    irq_set_exclusive_handler(uart_irq, uart_rx_isr);
    irq_set_priority(uart_irq, PICO_DEFAULT_IRQ_PRIORITY);
    irq_set_enabled(uart_irq, true);
    uart_set_irq_enables(UART_ID, true, false);

    while (true) {
        if (xQueueReceive(uart_rx_queue, &received, portMAX_DELAY) != pdPASS) {
            continue;
        }

        taskENTER_CRITICAL();
        rx_overflow_pending = uart_rx_overflow_pending;
        if (rx_overflow_pending) {
            xQueueReset(uart_rx_queue);
            uart_rx_overflow_pending = false;
            uart_rx_rearm_requested = true;
        }
        taskEXIT_CRITICAL();
        if (rx_overflow_pending) {
            length = 0U;
            overflow = false;
            uart_puts(UART_ID, "ERR LINE\r\n");
            continue;
        }

        if (received == '\r') {
            continue;
        }
        if (received != '\n') {
            if (!overflow && length < sizeof(command) - 1U) {
                command[length++] = (char)received;
            } else {
                overflow = true;
            }
            continue;
        }
        if (overflow) {
            uart_puts(UART_ID, "ERR LINE\r\n");
            overflow = false;
            length = 0U;
            continue;
        }

        command[length] = '\0';
        length = 0U;
        if (command[0] == '\0') {
            continue;
        }


            comando = INVALID_COMMAND;
            if (sscanf(command, "%5s", name) == 1) {
                if (strcmp(name, "V") == 0) { comando = V; }
                if (strcmp(name, "IL") == 0) { comando = IL; }
                if (strcmp(name, "VL") == 0) { comando = VL; }
                if (strcmp(name, "OUT") == 0) { comando = OUT; }
                if (strcmp(name, "RST") == 0) { comando = RST; }
                if (strcmp(name, "GET") == 0) { comando = GET; }
                if (strcmp(name, "SAVE") == 0) { comando = SAVE; }
            }

            uart_event = (uart_event_t){0};
            output = 0;

            switch (comando) {
                case GET:
                    taskENTER_CRITICAL();
                    measurements = latest_measurements;
                    settings = control_settings;
                    limits = runtime_settings;
                    overcurrent_fault = overcurrent_fault_latched;
                    overcurrent_fault2 = overcurrent_fault2_latched;
                    overvoltage_fault = overvoltage_fault_latched;
                    overvoltage_fault2 = overvoltage_fault2_latched;
                    duty_positive = actual_duty_percent;
                    duty_negative = actual_duty_percent2;
                    taskEXIT_CRITICAL();
                    ready_positive = measurements.valid;
                    ready_negative = measurements.valid2;
                    snprintf(response, sizeof(response), "OK VI=%.2f VP=%.2f VN=-%.2f IP=%.3f IN=%.3f SP=%.2f SN=-%.2f EP=%u EN=%u OCP=%u OCN=%u OVP=%u OVN=%u ILP=%.2f ILN=%.2f VLP=%.2f VLN=%.2f VPV=%u VNV=%u RP=%u RN=%u DP=%.2f DN=%.2f\r\n", measurements.vin, measurements.vout, measurements.vout2, measurements.iout, measurements.iout2, settings.voltage_setpoint, settings.voltage_setpoint2, (unsigned int)settings.output_enabled, (unsigned int)settings.output_enabled2, (unsigned int)overcurrent_fault, (unsigned int)overcurrent_fault2, (unsigned int)overvoltage_fault, (unsigned int)overvoltage_fault2, limits.overcurrent_limit, limits.overcurrent_limit2, limits.overvoltage_limit, limits.overvoltage_limit2, (unsigned int)measurements.valid, (unsigned int)measurements.valid2, (unsigned int)ready_positive, (unsigned int)ready_negative, duty_positive, duty_negative);
                    uart_puts(UART_ID, response);
                    continue;

                case V:
                    if (sscanf(command, "V %d %f", &output, &value) != 2) {
                        uart_puts(UART_ID, "ERR V\r\n");
                        continue;
                    }
                    if (!(value >= VOLTAGE_SETPOINT_MIN && value <= VOLTAGE_SETPOINT_MAX)) {
                        uart_puts(UART_ID, "ERR RANGE\r\n");
                        continue;
                    }
                    uart_event.comando = V;
                    uart_event.valor = value;
                    break;

                case IL:
                    if (sscanf(command, "IL %d %f", &output, &value) != 2) {
                        uart_puts(UART_ID, "ERR IL\r\n");
                        continue;
                    }
                    if (!(value >= CURRENT_LIMIT_MIN_AMPS && value <= CURRENT_LIMIT_MAX_AMPS)) {
                        uart_puts(UART_ID, "ERR RANGE\r\n");
                        continue;
                    }
                    uart_event.comando = IL;
                    uart_event.valor = value;
                    break;

                case VL:
                    if (sscanf(command, "VL %d %f", &output, &value) != 2) {
                        uart_puts(UART_ID, "ERR VL\r\n");
                        continue;
                    }
                    if (!(value >= OVERVOLTAGE_LIMIT_MIN_VOLTS && value <= VOUT_OVERVOLTAGE_TRIP)) {
                        uart_puts(UART_ID, "ERR RANGE\r\n");
                        continue;
                    }
                    uart_event.comando = VL;
                    uart_event.valor = value;
                    break;

                case OUT:
                    if (sscanf(command, "OUT %d %47s", &output, state) != 2) {
                        uart_puts(UART_ID, "ERR OUT\r\n");
                        continue;
                    }
                    if (output != 0 && output != 1) {
                        uart_puts(UART_ID, "ERR OUTPUT\r\n");
                        continue;
                    }
                    uart_event.comando = OUT;
                    if (strcmp(state, "ON") == 0) {
                        uart_event.valor = 1.0f;
                        break;
                    }
                    if (strcmp(state, "OFF") == 0) {
                        uart_event.valor = 0.0f;
                        break;
                    } else {
                        uart_puts(UART_ID, "ERR OUT\r\n");
                        continue;
                    }

                case RST:
                    if (sscanf(command, "RST %d", &output) != 1) {
                        uart_puts(UART_ID, "ERR RST\r\n");
                        continue;
                    }
                    uart_event.comando = RST;
                    break;

                case SAVE:
                    uart_event.comando = SAVE;
                    break;

                default:
                    uart_puts(UART_ID, "ERR COMMAND\r\n");
                    continue;
            }

            if (comando != SAVE) {
                if (output != 0 && output != 1) {
                    uart_puts(UART_ID, "ERR OUTPUT\r\n");
                    continue;
                }
                uart_event.salida = output;
            }

            central_event.type = CENTRAL_EVENT_UART_COMMAND;
            central_event.data.uart_command = uart_event;
            if (xQueueSend(central_event_queue, &central_event, 0U) != pdPASS) {
                uart_puts(UART_ID, "ERR BUSY\r\n");
                continue;
            }
            snprintf(response, sizeof(response), "OK QUEUED %s\r\n", command);
            uart_puts(UART_ID, response);
    }
}

static void display_task(void *parameters) {
    (void)parameters;

    control_frame_t frame = {0};
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(DISPLAY_UPDATE_MS);
    TickType_t last_usb_log_time = last_wake_time;
    const TickType_t usb_log_period_ticks = pdMS_TO_TICKS(USB_DIAGNOSTIC_PERIOD_MS);
    power_measurements_t usb_measurements;
    control_settings_t usb_settings;
    bool usb_overvoltage_fault;
    bool usb_overvoltage_fault2;
    bool usb_overcurrent_fault;
    bool usb_overcurrent_fault2;
    float usb_duty_percent;
    float usb_duty_percent2;
    bool usb_connected;
    bool usb_was_connected = false;
    TickType_t now;
    TickType_t usb_last_tick[2];
    bool usb_seen[2];
    uint32_t usb_age_ms[2];

    while (true) {
        control_frame_t received;
        while (xQueueReceive(display_queue, &received, 0U) == pdPASS) {
            frame = received;
        }
        lcd_show_values(&frame);

        usb_connected = stdio_usb_connected();
        if (usb_connected && !usb_was_connected) {
            printf("USB firmware_usuario_mediciones_corregidas v3 conectado\r\n");
        }
        usb_was_connected = usb_connected;

        now = xTaskGetTickCount();
        if (usb_connected && (TickType_t)(now - last_usb_log_time) >= usb_log_period_ticks) {
            taskENTER_CRITICAL();
            usb_measurements = latest_measurements;
            usb_settings = control_settings;
            usb_overvoltage_fault = overvoltage_fault_latched;
            usb_overvoltage_fault2 = overvoltage_fault2_latched;
            usb_overcurrent_fault = overcurrent_fault_latched;
            usb_overcurrent_fault2 = overcurrent_fault2_latched;
            usb_duty_percent = actual_duty_percent;
            usb_duty_percent2 = actual_duty_percent2;
            usb_last_tick[0] = usb_last_ads_tick[0];
            usb_last_tick[1] = usb_last_ads_tick[1];
            usb_seen[0] = usb_ads_seen[0];
            usb_seen[1] = usb_ads_seen[1];
            taskEXIT_CRITICAL();

            now = xTaskGetTickCount();
            usb_age_ms[0] = usb_diagnostic_age_ms(now, usb_last_tick[0], usb_seen[0]);
            usb_age_ms[1] = usb_diagnostic_age_ms(now, usb_last_tick[1], usb_seen[1]);
            printf("DBG VIN=%.3f VP=%.3f VN=-%.3f VRP=%.2f VRN=-%.2f ENP=%u ENN=%u VALP=%u VALN=%u AGEP=%lu AGEN=%lu OCP=%u OCN=%u OVP=%u OVN=%u DUTYP=%.2f DUTYN=%.2f\r\n",
                   usb_measurements.vin,
                   usb_measurements.vout,
                   usb_measurements.vout2,
                   usb_settings.voltage_setpoint,
                   usb_settings.voltage_setpoint2,
                   (unsigned int)usb_settings.output_enabled,
                   (unsigned int)usb_settings.output_enabled2,
                   (unsigned int)usb_measurements.valid,
                   (unsigned int)usb_measurements.valid2,
                   (unsigned long)usb_age_ms[0],
                   (unsigned long)usb_age_ms[1],
                   (unsigned int)usb_overcurrent_fault,
                   (unsigned int)usb_overcurrent_fault2,
                   (unsigned int)usb_overvoltage_fault,
                   (unsigned int)usb_overvoltage_fault2,
                   usb_duty_percent,
                   usb_duty_percent2);
            last_usb_log_time = now;
        }
        vTaskDelayUntil(&last_wake_time, period_ticks);
    }
}

// =====================================================
// Main
// =====================================================

int main(void) {
    char lcd_line[21];

    stdio_init_all();
    sleep_ms(500);

    uart_init(UART_ID, UART_BAUD_RATE);
    gpio_set_function(UART_TX_GPIO, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_GPIO, GPIO_FUNC_UART);

    i2c_init(I2C_PORT, 100U * 1000U);
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_GPIO);
    gpio_pull_up(SCL_GPIO);

    stored_settings_t saved_settings;
    if (settings_storage_load(&saved_settings)) {
        runtime_settings.overvoltage_limit =    clamp_float(saved_settings.overvoltage_positive,    OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
        runtime_settings.overvoltage_limit2 =   clamp_float(saved_settings.overvoltage_negative,    OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
        runtime_settings.overcurrent_limit =    clamp_float(saved_settings.current_positive,        CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
        runtime_settings.overcurrent_limit2 =   clamp_float(saved_settings.current_negative,        CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
        control_settings.voltage_setpoint =     clamp_float(saved_settings.voltage_positive,        VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit);
        control_settings.voltage_setpoint2 =    clamp_float(saved_settings.voltage_negative,        VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit2);
    }

    if (!pwm_generator_init(PWM_GEN_PIN, PWM_FREQUENCY_HZ)) {
        return 1;
    }

    i2c_mutex = xSemaphoreCreateMutex();

    flash_queue =           xQueueCreate(FLASH_QUEUE_LENGTH,        sizeof(stored_settings_t));
    adc_raw_queue =         xQueueCreate(ADC_RAW_QUEUE_LENGTH,      sizeof(adc_sample_t));
    protection_queue =      xQueueCreate(PROTECTION_QUEUE_LENGTH,   sizeof(adc_sample_t));
    central_event_queue =   xQueueCreate(CENTRAL_EVENT_QUEUE_LENGTH, sizeof(central_event_t));
    control_positive_queue =xQueueCreate(1U,                        sizeof(control_frame_t));
    control_negative_queue =xQueueCreate(1U,                        sizeof(control_frame_t));
    display_queue =         xQueueCreate(1U,                        sizeof(control_frame_t));
    encoder_queue =         xQueueCreate(ENCODER_QUEUE_LENGTH,      sizeof(encoder_event_t));
    uart_rx_queue =         xQueueCreate(UART_RX_QUEUE_LENGTH,      sizeof(uint8_t));

    lcd_init(I2C_PORT, LCD_ADDR);
    lcd_clear();
    snprintf(lcd_line, sizeof(lcd_line), "%-20.20s", "FUENTE PARTIDA PID");

    lcd_set_cursor(0, 0);
    lcd_string(lcd_line);
    snprintf(lcd_line, sizeof(lcd_line), "%-20.20s", "ADS:Vo ADC:Vi/Io");

    lcd_set_cursor(1, 0);
    lcd_string(lcd_line);
    snprintf(lcd_line, sizeof(lcd_line), "%-20.20s", "Encoders: 2-4/6-8");

    lcd_set_cursor(2, 0);
    lcd_string(lcd_line);
    snprintf(lcd_line, sizeof(lcd_line), "%-20.20s", "Inicializando...");

    lcd_set_cursor(3, 0);
    lcd_string(lcd_line);

    sleep_ms(1000);

    snprintf(lcd_line, sizeof(lcd_line), "%-20.20s", "Creando tareas...");
    lcd_set_cursor(3, 0);
    lcd_string(lcd_line);

    xTaskCreate(protection_task,         "PROTECTION",   TASK_STACK_PROTECTION_WORDS,    NULL, TASK_PRIORITY_PROTECTION,    NULL);
    xTaskCreate(adc_internal_task,       "ADC_INT",      TASK_STACK_ADC_INTERNAL_WORDS,  NULL, TASK_PRIORITY_ADC_INTERNAL,  NULL);
    xTaskCreate(central_task,            "CENTRAL",      TASK_STACK_ADC_WORDS,           NULL, TASK_PRIORITY_ADC,           NULL);
    xTaskCreate(ads1115_task,            "ADS",          TASK_STACK_ADS_WORDS,           NULL, TASK_PRIORITY_ADS,           NULL);
    xTaskCreate(control_positive_task,   "CONTROL_POS",  TASK_STACK_CONTROL_WORDS,       NULL, TASK_PRIORITY_CONTROL,       NULL);
    xTaskCreate(control_negative_task,   "CONTROL_NEG",  TASK_STACK_CONTROL2_WORDS,      NULL, TASK_PRIORITY_CONTROL2,      NULL);
    xTaskCreate(encoder_task,            "ENCODER",      TASK_STACK_ENCODER_WORDS,       NULL, TASK_PRIORITY_ENCODER,       NULL);
    xTaskCreate(uart_task,               "UART",         TASK_STACK_UART_WORDS,          NULL, TASK_PRIORITY_UART,          NULL);
    xTaskCreate(display_task,            "DISPLAY",      TASK_STACK_DISPLAY_WORDS,       NULL, TASK_PRIORITY_DISPLAY,       NULL);
    xTaskCreate(flash_task,              "FLASH",        TASK_STACK_FLASH_WORDS,         NULL, TASK_PRIORITY_FLASH,         NULL);

    init_encoder_interrupts();
    init_adc_interrupt();

    vTaskStartScheduler();
    return 0;
}

