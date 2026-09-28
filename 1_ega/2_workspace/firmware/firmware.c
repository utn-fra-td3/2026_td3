#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"

#include "FreeRTOS.h"
#include "task.h"
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
#define CONTROL_PERIOD_MS 5U
#define CONTROL2_PERIOD_MS 5U
#define ENCODER_PERIOD_MS 1U
#define ENCODER_BUTTON_DEBOUNCE_MS 30U
#define ENCODER_LONG_PRESS_MS 800U
#define MEASUREMENT_STALE_MS 150U
#define DISPLAY_UPDATE_MS 100U
#define UART_PERIOD_MS 2U

#define VOLTAGE_SETPOINT_MIN 5.0f
#define VOLTAGE_SETPOINT_MAX 30.0f
#define VOLTAGE_SETPOINT_INITIAL 10.0f
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
#define CURRENT_FILTER_ALPHA 0.35f

#define CURRENT2_SHUNT_OHMS 0.100f
#define CURRENT2_SENSE_GAIN (200000.0f / 20000.0f)
#define CURRENT_NEG_ZERO_VOLTS 0.0015f
#define CURRENT2_FILTER_ALPHA 0.35f
#define CURRENT_ZERO_DEADBAND_AMPS 0.010f

#define DUTY_MIN_PERCENT 0.0f
#define DUTY_MAX_PERCENT 70.0f

#define SOFT_START_TIME_SECONDS 2.00f

#define PID_KP 0.80f
#define PID_KI 2.00f

#define VOLTAGE_FILTER_ALPHA 0.40f

#define VIN_ADC_GPIO 26
#define VIN_ADC_CHANNEL 0
#define IOUT2_ADC_GPIO 27
#define IOUT2_ADC_CHANNEL 1
#define IOUT1_ADC_GPIO 28
#define IOUT1_ADC_CHANNEL 2

#define ADC_ROUND_ROBIN_MASK ((1U << VIN_ADC_CHANNEL) | (1U << IOUT2_ADC_CHANNEL) | (1U << IOUT1_ADC_CHANNEL))
#define ADC_FIFO_CLOCK_DIVIDER 1600.0f
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

#define TASK_STACK_ADC_WORDS 512U
#define TASK_STACK_CONTROL_WORDS 768U
#define TASK_STACK_CONTROL2_WORDS 768U
#define TASK_STACK_ENCODER_WORDS 512U
#define TASK_STACK_DISPLAY_WORDS 1024U
#define TASK_STACK_UART_WORDS 768U

#define TASK_PRIORITY_DISPLAY (tskIDLE_PRIORITY + 1U)
#define TASK_PRIORITY_UART (tskIDLE_PRIORITY + 2U)
#define TASK_PRIORITY_ADC (tskIDLE_PRIORITY + 3U)
#define TASK_PRIORITY_ENCODER (tskIDLE_PRIORITY + 3U)
#define TASK_PRIORITY_CONTROL (tskIDLE_PRIORITY + 4U)
#define TASK_PRIORITY_CONTROL2 (tskIDLE_PRIORITY + 4U)

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
    power_measurements_t measurements;
    float duty_percent;
    float duty_percent2;
    float voltage_setpoint;
    float voltage_setpoint2;
    bool output_enabled;
    bool output_enabled2;
    bool overvoltage_fault;
    bool overvoltage_fault2;
    bool overcurrent_fault;
    bool overcurrent_fault2;
    float overcurrent_limit;
    float overcurrent_limit2;
    float overvoltage_limit;
    float overvoltage_limit2;
    uint8_t edit_mode;
    uint8_t edit_mode2;
} system_snapshot_t;

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
static volatile uint16_t adc_latest_raw[3] = {0U, 0U, 0U};
static volatile uint16_t overcurrent_raw_limit = ADC_MAX_VALUE;
static volatile uint16_t overcurrent_raw_limit2 = ADC_MAX_VALUE;
static volatile encoder_edit_mode_t encoder_edit_mode[2] = {EDIT_VOLTAGE_SETPOINT, EDIT_VOLTAGE_SETPOINT};

static control_settings_t control_settings = {
    VOLTAGE_SETPOINT_INITIAL,
    VOLTAGE_SETPOINT_INITIAL,
    false,
    false
};

static runtime_settings_t runtime_settings = {
    OVERCURRENT_TRIP_AMPS,
    OVERCURRENT_TRIP_AMPS,
    VOUT_OVERVOLTAGE_TRIP,
    VOUT_OVERVOLTAGE_TRIP
};

static uint8_t adc_fifo_channel = VIN_ADC_CHANNEL;
static uint16_t previous_vin_raw = 0U;
static uint16_t vin_fast_drop_raw = 0U;
static uint16_t vin_fast_drop_min_raw = 0U;

// =====================================================
// Sincronización FreeRTOS
// =====================================================

static SemaphoreHandle_t i2c_mutex = NULL;
static SemaphoreHandle_t state_mutex = NULL;

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

static uint16_t current_limit_to_raw(float limit_amps, float shunt_ohms, float amplifier_gain, float zero_volts) {
    float pin_voltage = zero_volts + limit_amps * shunt_ohms * amplifier_gain;
    float raw = pin_voltage * ADC_MAX_VALUE / ADC_REFERENCE_VOLTAGE;
    return (uint16_t)clamp_float(raw, 0.0f, ADC_MAX_VALUE);
}

static TickType_t milliseconds_to_ticks_min(uint32_t milliseconds) {
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (ticks == 0U) {
        ticks = 1U;
    }
    return ticks;
}

static void update_measurements(const power_measurements_t *measurements) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    latest_measurements = *measurements;
    xSemaphoreGive(state_mutex);
}

static power_measurements_t get_latest_measurements(void) {
    power_measurements_t copy;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    copy = latest_measurements;
    xSemaphoreGive(state_mutex);
    return copy;
}

static control_settings_t get_control_settings(void) {
    control_settings_t copy;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    copy = control_settings;
    xSemaphoreGive(state_mutex);
    return copy;
}

static void encoder_adjust_selected(uint8_t encoder_index, int direction) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    float *setpoint;
    float *current_limit;
    float *voltage_limit;
    if (encoder_index == 0U) {
        setpoint = &control_settings.voltage_setpoint;
        current_limit = &runtime_settings.overcurrent_limit;
        voltage_limit = &runtime_settings.overvoltage_limit;
    } else {
        setpoint = &control_settings.voltage_setpoint2;
        current_limit = &runtime_settings.overcurrent_limit2;
        voltage_limit = &runtime_settings.overvoltage_limit2;
    }
    switch (encoder_edit_mode[encoder_index]) {
        case EDIT_CURRENT_LIMIT:
            *current_limit = clamp_float(*current_limit + (float)direction * CURRENT_LIMIT_STEP_AMPS, CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
            if (encoder_index == 0U) {
                overcurrent_raw_limit = current_limit_to_raw(*current_limit, CURRENT_SHUNT_OHMS, CURRENT_SENSE_GAIN, CURRENT_POS_ZERO_VOLTS);
            } else {
                overcurrent_raw_limit2 = current_limit_to_raw(*current_limit, CURRENT2_SHUNT_OHMS, CURRENT2_SENSE_GAIN, CURRENT_NEG_ZERO_VOLTS);
            }
            break;
        case EDIT_OVERVOLTAGE_LIMIT:
            *voltage_limit = clamp_float(*voltage_limit + (float)direction * OVERVOLTAGE_LIMIT_STEP_VOLTS, OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
            if (*setpoint > *voltage_limit) {
                *setpoint = *voltage_limit;
            }
            break;
        case EDIT_VOLTAGE_SETPOINT:
        default:
            *setpoint = clamp_float(*setpoint + (float)direction * VOLTAGE_SETPOINT_STEP, VOLTAGE_SETPOINT_MIN, *voltage_limit);
            break;
    }
    xSemaphoreGive(state_mutex);
}

static void encoder_select_next_parameter(uint8_t encoder_index) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    encoder_edit_mode[encoder_index] = (encoder_edit_mode_t)((encoder_edit_mode[encoder_index] + 1U) % EDIT_MODE_COUNT);
    xSemaphoreGive(state_mutex);
}

static void encoder_toggle_output(uint8_t encoder_index) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    if (encoder_index == 0U) {
        if (overcurrent_fault_latched || overvoltage_fault_latched) {
            overcurrent_fault_latched = false;
            overvoltage_fault_latched = false;
            control_settings.output_enabled = false;
        } else {
            control_settings.output_enabled = !control_settings.output_enabled;
        }
    } else {
        if (overcurrent_fault2_latched || overvoltage_fault2_latched) {
            overcurrent_fault2_latched = false;
            overvoltage_fault2_latched = false;
            control_settings.output_enabled2 = false;
        } else {
            control_settings.output_enabled2 = !control_settings.output_enabled2;
        }
    }
    xSemaphoreGive(state_mutex);
}

static void latch_overvoltage_fault(void) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    overvoltage_fault_latched = true;
    control_settings.output_enabled = false;
    pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
    actual_duty_percent = 0.0f;
    xSemaphoreGive(state_mutex);
}

static void latch_overvoltage_fault2(void) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    overvoltage_fault2_latched = true;
    control_settings.output_enabled2 = false;
    pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
    actual_duty_percent2 = 0.0f;
    xSemaphoreGive(state_mutex);
}

static system_snapshot_t get_system_snapshot(void) {
    system_snapshot_t snapshot;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    taskENTER_CRITICAL();
    snapshot.measurements = latest_measurements;
    snapshot.duty_percent = actual_duty_percent;
    snapshot.duty_percent2 = actual_duty_percent2;
    snapshot.voltage_setpoint = control_settings.voltage_setpoint;
    snapshot.voltage_setpoint2 = control_settings.voltage_setpoint2;
    snapshot.output_enabled = control_settings.output_enabled;
    snapshot.output_enabled2 = control_settings.output_enabled2;
    snapshot.overvoltage_fault = overvoltage_fault_latched;
    snapshot.overvoltage_fault2 = overvoltage_fault2_latched;
    snapshot.overcurrent_fault = overcurrent_fault_latched;
    snapshot.overcurrent_fault2 = overcurrent_fault2_latched;
    snapshot.overcurrent_limit = runtime_settings.overcurrent_limit;
    snapshot.overcurrent_limit2 = runtime_settings.overcurrent_limit2;
    snapshot.overvoltage_limit = runtime_settings.overvoltage_limit;
    snapshot.overvoltage_limit2 = runtime_settings.overvoltage_limit2;
    snapshot.edit_mode = (uint8_t)encoder_edit_mode[0];
    snapshot.edit_mode2 = (uint8_t)encoder_edit_mode[1];
    taskEXIT_CRITICAL();
    xSemaphoreGive(state_mutex);
    return snapshot;
}

static void lcd_show_values(const system_snapshot_t *snapshot) {
    char line[21];
    char voltage_pos[6];
    char voltage_neg[6];
    char current_pos[6];
    char current_neg[6];
    const char *state_pos = "OF";
    const char *state_neg = "OF";
    if (snapshot->output_enabled) {
        state_pos = "ON";
    }
    if (snapshot->output_enabled2) {
        state_neg = "ON";
    }

    snprintf(line, sizeof(line), "Vi:%5.2f +%s -%s    ", snapshot->measurements.vin, state_pos, state_neg);
    lcd_set_cursor(0, 0);
    lcd_string(line);

    if (snapshot->overvoltage_fault) {
        snprintf(voltage_pos, sizeof(voltage_pos), "**OV*");
    } else {
        snprintf(voltage_pos, sizeof(voltage_pos), "%5.2f", snapshot->measurements.vout);
    }
    if (snapshot->overvoltage_fault2) {
        snprintf(voltage_neg, sizeof(voltage_neg), "**OV*");
    } else {
        snprintf(voltage_neg, sizeof(voltage_neg), "%5.2f", snapshot->measurements.vout2);
    }
    snprintf(line, sizeof(line), "V+:%s V-:%s   ", voltage_pos, voltage_neg);
    lcd_set_cursor(1, 0);
    lcd_string(line);

    if (snapshot->overcurrent_fault) {
        snprintf(current_pos, sizeof(current_pos), "**OC*");
    } else {
        snprintf(current_pos, sizeof(current_pos), "%5.2f", snapshot->measurements.iout);
    }
    if (snapshot->overcurrent_fault2) {
        snprintf(current_neg, sizeof(current_neg), "**OC*");
    } else {
        snprintf(current_neg, sizeof(current_neg), "%5.2f", snapshot->measurements.iout2);
    }
    snprintf(line, sizeof(line), "I+:%s I-:%s   ", current_pos, current_neg);
    lcd_set_cursor(2, 0);
    lcd_string(line);

    char edit_pos[10];
    char edit_neg[10];
    if (snapshot->edit_mode == EDIT_CURRENT_LIMIT) {
        snprintf(edit_pos, sizeof(edit_pos), "IL+:%4.2f", snapshot->overcurrent_limit);
    } else if (snapshot->edit_mode == EDIT_OVERVOLTAGE_LIMIT) {
        snprintf(edit_pos, sizeof(edit_pos), "OV+:%4.1f", snapshot->overvoltage_limit);
    } else {
        snprintf(edit_pos, sizeof(edit_pos), "V+:%4.1f", snapshot->voltage_setpoint);
    }
    if (snapshot->edit_mode2 == EDIT_CURRENT_LIMIT) {
        snprintf(edit_neg, sizeof(edit_neg), "IL-:%4.2f", snapshot->overcurrent_limit2);
    } else if (snapshot->edit_mode2 == EDIT_OVERVOLTAGE_LIMIT) {
        snprintf(edit_neg, sizeof(edit_neg), "OV-:%4.1f", snapshot->overvoltage_limit2);
    } else {
        snprintf(edit_neg, sizeof(edit_neg), "V-:%4.1f", snapshot->voltage_setpoint2);
    }
    snprintf(line, sizeof(line), "%-9s%-9s  ", edit_pos, edit_neg);
    lcd_set_cursor(3, 0);
    lcd_string(line);
}

// =====================================================
// Medición y protección
// =====================================================

static void adc_fifo_irq_handler(void) {
    while (!adc_fifo_is_empty()) {
        uint16_t raw = adc_fifo_get() & 0x0FFFU;
        uint8_t channel = adc_fifo_channel;
        adc_latest_raw[channel] = raw;

        if (channel == IOUT1_ADC_CHANNEL && control_settings.output_enabled && raw >= overcurrent_raw_limit) {
            overcurrent_fault_latched = true;
            control_settings.output_enabled = false;
            pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
            actual_duty_percent = 0.0f;
        } else if (channel == IOUT2_ADC_CHANNEL && control_settings.output_enabled2 && raw >= overcurrent_raw_limit2) {
            overcurrent_fault2_latched = true;
            control_settings.output_enabled2 = false;
            pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
            actual_duty_percent2 = 0.0f;
        } else if (channel == VIN_ADC_CHANNEL) {
            bool fast_drop = (control_settings.output_enabled || control_settings.output_enabled2) && previous_vin_raw >= vin_fast_drop_min_raw && previous_vin_raw > raw && (previous_vin_raw - raw) >= vin_fast_drop_raw;
            previous_vin_raw = raw;
            if (fast_drop) {
                overcurrent_fault_latched = true;
                overcurrent_fault2_latched = true;
                control_settings.output_enabled = false;
                control_settings.output_enabled2 = false;
                pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
                pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
                actual_duty_percent = 0.0f;
                actual_duty_percent2 = 0.0f;
            }
        }

        if (channel == IOUT1_ADC_CHANNEL) {
            adc_fifo_channel = VIN_ADC_CHANNEL;
        } else {
            adc_fifo_channel = (uint8_t)(channel + 1U);
        }
    }
}

static void adc_fifo_protection_init(void) {
    float vin_counts_per_volt = ADC_MAX_VALUE * VIN_R2_OHMS / (ADC_REFERENCE_VOLTAGE * (VIN_R1_OHMS + VIN_R2_OHMS));
    vin_fast_drop_raw = (uint16_t)(VIN_FAST_DROP_VOLTS * vin_counts_per_volt);
    vin_fast_drop_min_raw = (uint16_t)(VIN_FAST_DROP_MIN_VOLTS * vin_counts_per_volt);
    overcurrent_raw_limit = current_limit_to_raw(runtime_settings.overcurrent_limit, CURRENT_SHUNT_OHMS, CURRENT_SENSE_GAIN, CURRENT_POS_ZERO_VOLTS);
    overcurrent_raw_limit2 = current_limit_to_raw(runtime_settings.overcurrent_limit2, CURRENT2_SHUNT_OHMS, CURRENT2_SENSE_GAIN, CURRENT_NEG_ZERO_VOLTS);

    adc_run(false);
    adc_set_round_robin(0U);
    adc_fifo_drain();
    adc_fifo_channel = VIN_ADC_CHANNEL;
    adc_select_input(VIN_ADC_CHANNEL);
    adc_set_round_robin(ADC_ROUND_ROBIN_MASK);
    adc_set_clkdiv(ADC_FIFO_CLOCK_DIVIDER);
    adc_fifo_setup(true, false, 1U, false, false);
    irq_set_exclusive_handler(ADC_IRQ_FIFO, adc_fifo_irq_handler);
    irq_set_enabled(ADC_IRQ_FIFO, true);
    adc_irq_set_enabled(true);
    adc_run(true);
}

static void read_vin_raw(power_measurements_t *measurements) {
    measurements->vin_raw = adc_latest_raw[VIN_ADC_CHANNEL];
    measurements->vin = vin_from_adc_raw(measurements->vin_raw);
}

static bool ads1115_start_conversion(uint8_t channel) {
    uint16_t config = 0U;
    config |= 0x8000U;
    config |= (uint16_t)((0x04U | (channel & 0x03U)) << 12);
    config |= ADS1115_PGA_BITS;
    config |= 0x0100U;
    config |= ADS1115_DR_BITS;
    config |= 0x0003U;
    uint8_t buffer[3] = {ADS1115_REG_CONFIG, (uint8_t)(config >> 8), (uint8_t)(config & 0xFFU)};
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    int written = i2c_write_blocking(I2C_PORT, ADS1115_ADDR, buffer, 3, false);
    xSemaphoreGive(i2c_mutex);
    return written == 3;
}

static bool ads1115_read_result(int16_t *result) {
    uint8_t register_address = ADS1115_REG_CONVERSION;
    uint8_t rx[2] = {0};
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    int written = i2c_write_blocking(I2C_PORT, ADS1115_ADDR, &register_address, 1, true);
    int read = -1;
    if (written == 1) {
        read = i2c_read_blocking(I2C_PORT, ADS1115_ADDR, rx, 2, false);
    }
    xSemaphoreGive(i2c_mutex);
    if (written != 1 || read != 2) {
        return false;
    }
    *result = (int16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    return true;
}

static bool ads1115_read_channel_raw(uint8_t channel, uint16_t *raw_result) {
    if (raw_result == NULL) {
        return false;
    }
    if (!ads1115_start_conversion(channel)) {
        return false;
    }
    vTaskDelay(milliseconds_to_ticks_min(ADS1115_CONV_DELAY_MS));
    int16_t raw = 0;
    if (!ads1115_read_result(&raw)) {
        return false;
    }
    if (raw < 0) {
        raw = 0;
    }
    *raw_result = (uint16_t)raw;
    return true;
}

static void read_ads_measurements_raw(power_measurements_t *measurements) {
    uint16_t vout1_raw = 0U;
    uint16_t vout2_raw = 0U;
    uint16_t iout1_raw = adc_latest_raw[IOUT1_ADC_CHANNEL];
    uint16_t iout2_raw = adc_latest_raw[IOUT2_ADC_CHANNEL];
    bool vout1_ok = ads1115_read_channel_raw(ADS_CH_VOUT1, &vout1_raw);
    measurements->vout_raw = vout1_raw;
    measurements->iout_raw = iout1_raw;
    measurements->iout2_raw = iout2_raw;
    measurements->valid = vout1_ok;
    if (measurements->valid) {
        measurements->vout = vout_pos_from_ads_raw(vout1_raw);
        measurements->iout = (((float)iout1_raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_POS_ZERO_VOLTS) / (CURRENT_SHUNT_OHMS * CURRENT_SENSE_GAIN);
        if (measurements->iout < CURRENT_ZERO_DEADBAND_AMPS) {
            measurements->iout = 0.0f;
        }
        if (measurements->vout > runtime_settings.overvoltage_limit) {
            latch_overvoltage_fault();
        }
    }

    bool vout2_ok = ads1115_read_channel_raw(ADS_CH_VOUT2, &vout2_raw);
    measurements->vout2_raw = vout2_raw;
    measurements->valid2 = vout2_ok;
    if (measurements->valid2) {
        measurements->vout2 = vout_neg_from_ads_raw(vout2_raw);
        measurements->iout2 = (((float)iout2_raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE) - CURRENT_NEG_ZERO_VOLTS) / (CURRENT2_SHUNT_OHMS * CURRENT2_SENSE_GAIN);
        if (measurements->iout2 < CURRENT_ZERO_DEADBAND_AMPS) {
            measurements->iout2 = 0.0f;
        }
        if (measurements->vout2 > runtime_settings.overvoltage_limit2) {
            latch_overvoltage_fault2();
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
    if (overcurrent_fault_latched || overvoltage_fault_latched) {
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
    if (overcurrent_fault2_latched || overvoltage_fault2_latched) {
        safe_duty = 0.0f;
        level = 0U;
    }
    pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, (uint16_t)level);
    actual_duty_percent2 = safe_duty;
    taskEXIT_CRITICAL();
}

static void pwm_generator_init(void) {
    gpio_set_function(PWM_GEN_PIN, GPIO_FUNC_PWM);
    gpio_set_function(PWM_GEN2_PIN, GPIO_FUNC_PWM);
    pwm_gen_slice = pwm_gpio_to_slice_num(PWM_GEN_PIN);
    pwm_gen_channel = pwm_gpio_to_channel(PWM_GEN_PIN);
    pwm_gen2_channel = pwm_gpio_to_channel(PWM_GEN2_PIN);
    pwm_gen_wrap = (uint16_t)(clock_get_hz(clk_sys) / PWM_FREQUENCY_HZ - 1U);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 1.0f);
    pwm_config_set_wrap(&config, pwm_gen_wrap);
    pwm_init(pwm_gen_slice, &config, false);
    pwm_set_chan_level(pwm_gen_slice, pwm_gen_channel, 0U);
    pwm_set_chan_level(pwm_gen_slice, pwm_gen2_channel, 0U);
    pwm_set_enabled(pwm_gen_slice, true);
    actual_duty_percent = 0.0f;
    actual_duty_percent2 = 0.0f;
}

// =====================================================
// Tareas de adquisición y control
// =====================================================

static void encoder_task(void *parameters) {
    (void)parameters;
    encoder_t encoders[2];
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    encoder_init(&encoders[0], ENCODER1_CLK_GPIO, ENCODER1_DT_GPIO, ENCODER1_SW_GPIO, ENCODER_BUTTON_DEBOUNCE_MS, ENCODER_LONG_PRESS_MS, now_ms);
    encoder_init(&encoders[1], ENCODER2_CLK_GPIO, ENCODER2_DT_GPIO, ENCODER2_SW_GPIO, ENCODER_BUTTON_DEBOUNCE_MS, ENCODER_LONG_PRESS_MS, now_ms);
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = milliseconds_to_ticks_min(ENCODER_PERIOD_MS);
    while (true) {
        now_ms = to_ms_since_boot(get_absolute_time());
        int movement1 = encoder_poll_rotation(&encoders[0]);
        int movement2 = encoder_poll_rotation(&encoders[1]);
        if (movement1 != 0) {
            encoder_adjust_selected(0U, movement1 * ENCODER1_DIRECTION);
        }
        if (movement2 != 0) {
            encoder_adjust_selected(1U, movement2 * ENCODER2_DIRECTION);
        }
        encoder_button_event_t button1 = encoder_poll_button(&encoders[0], now_ms);
        encoder_button_event_t button2 = encoder_poll_button(&encoders[1], now_ms);
        if (button1 == ENCODER_BUTTON_EVENT_SHORT) {
            encoder_select_next_parameter(0U);
        } else if (button1 == ENCODER_BUTTON_EVENT_LONG) {
            encoder_toggle_output(0U);
        }
        if (button2 == ENCODER_BUTTON_EVENT_SHORT) {
            encoder_select_next_parameter(1U);
        } else if (button2 == ENCODER_BUTTON_EVENT_LONG) {
            encoder_toggle_output(1U);
        }
        vTaskDelayUntil(&last_wake_time, period_ticks);
    }
}

static void adc_task(void *parameters) {
    (void)parameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = milliseconds_to_ticks_min(ADC_PERIOD_MS);
    bool first_vin = true;
    bool first_measurement1 = true;
    bool first_measurement2 = true;
    TickType_t last_measurement1_ok = last_wake_time;
    TickType_t last_measurement2_ok = last_wake_time;
    const TickType_t stale_ticks = milliseconds_to_ticks_min(MEASUREMENT_STALE_MS);
    power_measurements_t filtered = {0};
   
    while (true) {
        TickType_t now = xTaskGetTickCount();
        power_measurements_t raw = {0};
        read_vin_raw(&raw);
        read_ads_measurements_raw(&raw);
        if (first_vin) {
            filtered.vin_raw = raw.vin_raw;
            filtered.vin = raw.vin;
            first_vin = false;
        } else {
            filtered.vin_raw = raw.vin_raw;
            filtered.vin += VOLTAGE_FILTER_ALPHA * (raw.vin - filtered.vin);
        }
        if (raw.valid) {
            filtered.vout_raw = raw.vout_raw;
            filtered.iout_raw = raw.iout_raw;
            if (first_measurement1) {
                filtered.vout = raw.vout;
                filtered.iout = raw.iout;
                first_measurement1 = false;
            } else {
                filtered.vout += VOLTAGE_FILTER_ALPHA * (raw.vout - filtered.vout);
                filtered.iout += CURRENT_FILTER_ALPHA * (raw.iout - filtered.iout);
            }
            filtered.valid = true;
            last_measurement1_ok = now;
        } else if (first_measurement1 || (now - last_measurement1_ok) >= stale_ticks) {
            filtered.valid = false;
        }
        if (raw.valid2) {
            filtered.vout2_raw = raw.vout2_raw;
            filtered.iout2_raw = raw.iout2_raw;
            if (first_measurement2) {
                filtered.vout2 = raw.vout2;
                filtered.iout2 = raw.iout2;
                first_measurement2 = false;
            } else {
                filtered.vout2 += VOLTAGE_FILTER_ALPHA * (raw.vout2 - filtered.vout2);
                filtered.iout2 += CURRENT2_FILTER_ALPHA * (raw.iout2 - filtered.iout2);
            }
            filtered.valid2 = true;
            last_measurement2_ok = now;
        } else if (first_measurement2 || (now - last_measurement2_ok) >= stale_ticks) {
            filtered.valid2 = false;
        }
        update_measurements(&filtered);
        vTaskDelayUntil(&last_wake_time, period_ticks);
    }
}

static void control_task(void *parameters) {
    bool negative = parameters != NULL;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
    const float dt = (float)CONTROL_PERIOD_MS / 1000.0f;
    uint64_t soft_start_us = time_us_64();
    bool was_enabled = false;
    float integral = 0.0f;

    while (true) {
        uint64_t now_us = time_us_64();
        power_measurements_t m = get_latest_measurements();
        control_settings_t s = get_control_settings();
        bool valid;
        bool enabled;
        bool overvoltage;
        bool overcurrent;
        float vout;
        float target;
        float voltage_limit;
        if (negative) {
            valid = m.valid2;
            enabled = s.output_enabled2;
            overvoltage = overvoltage_fault2_latched;
            overcurrent = overcurrent_fault2_latched;
            vout = m.vout2;
            target = s.voltage_setpoint2;
            voltage_limit = runtime_settings.overvoltage_limit2;
        } else {
            valid = m.valid;
            enabled = s.output_enabled;
            overvoltage = overvoltage_fault_latched;
            overcurrent = overcurrent_fault_latched;
            vout = m.vout;
            target = s.voltage_setpoint;
            voltage_limit = runtime_settings.overvoltage_limit;
        }
        if (!valid || !enabled || overcurrent || overvoltage) {
            if (negative) {
                pwm_set_duty2_safe(0.0f);
            } else {
                pwm_set_duty_safe(0.0f);
            }
            integral = 0.0f;
            was_enabled = false;
            vTaskDelayUntil(&last_wake, period);
            continue;
        }

        if (!was_enabled) {
            soft_start_us = now_us;
            was_enabled = true;
        }

        if (vout >= voltage_limit) {
            if (negative)
                latch_overvoltage_fault2();
            else
                latch_overvoltage_fault();
            continue;
        }

        float ramp = clamp_float((float)(now_us - soft_start_us) / (1000000.0f * SOFT_START_TIME_SECONDS), 0.0f, 1.0f);
        target = clamp_float(target, VOLTAGE_SETPOINT_MIN, MAXIMUM_VOLTAGE_REFERENCE);
        float reference = ramp * target;
        float error = reference - vout;
        integral = clamp_float(integral + error * dt, 0.0f, DUTY_MAX_PERCENT / PID_KI);
        float duty = clamp_float(PID_KP * error + PID_KI * integral, DUTY_MIN_PERCENT, DUTY_MAX_PERCENT);

        if (negative)
            pwm_set_duty2_safe(duty);
        else
            pwm_set_duty_safe(duty);

        vTaskDelayUntil(&last_wake, period);
    }
}

// =====================================================
// Comunicación UART
// =====================================================

static void uart_process_command(char *command) {
    char response[224];
    char branch;
    char state[4];
    float value;
    bool turn_on;
    bool fault;
    float voltage_limit;
    system_snapshot_t snapshot = get_system_snapshot();

    if (strcmp(command, "GET") == 0) {
        snprintf(response, sizeof(response), "OK VI=%.2f VP=%.2f VN=-%.2f IP=%.3f IN=%.3f SP=%.2f SN=-%.2f EP=%u EN=%u OCP=%u OCN=%u OVP=%u OVN=%u ILP=%.2f ILN=%.2f\r\n", snapshot.measurements.vin, snapshot.measurements.vout, snapshot.measurements.vout2, snapshot.measurements.iout, snapshot.measurements.iout2, snapshot.voltage_setpoint, snapshot.voltage_setpoint2, (unsigned int)snapshot.output_enabled, (unsigned int)snapshot.output_enabled2, (unsigned int)snapshot.overcurrent_fault, (unsigned int)snapshot.overcurrent_fault2, (unsigned int)snapshot.overvoltage_fault, (unsigned int)snapshot.overvoltage_fault2, snapshot.overcurrent_limit, snapshot.overcurrent_limit2);
        uart_puts(UART_ID, response);
        return;
    }

    if (sscanf(command, "SET %c %f", &branch, &value) == 2) {
        if (branch == 'P') {
            voltage_limit = snapshot.overvoltage_limit;
        } else if (branch == 'N') {
            voltage_limit = snapshot.overvoltage_limit2;
        } else {
            uart_puts(UART_ID, "ERR SET\r\n");
            return;
        }
        if (!(value >= VOLTAGE_SETPOINT_MIN && value <= VOLTAGE_SETPOINT_MAX && value <= voltage_limit)) {
            uart_puts(UART_ID, "ERR RANGE\r\n");
            return;
        }
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (branch == 'P') {
            control_settings.voltage_setpoint = value;
        } else {
            control_settings.voltage_setpoint2 = value;
        }
        xSemaphoreGive(state_mutex);
        snprintf(response, sizeof(response), "OK SET %c %.2f\r\n", branch, value);
        uart_puts(UART_ID, response);
        return;
    }

    if (sscanf(command, "OUT %c %3s", &branch, state) == 2) {
        if (strcmp(state, "ON") == 0) {
            turn_on = true;
        } else if (strcmp(state, "OFF") == 0) {
            turn_on = false;
        } else {
            uart_puts(UART_ID, "ERR OUT\r\n");
            return;
        }
        if (branch == 'P') {
            fault = snapshot.overcurrent_fault || snapshot.overvoltage_fault;
        } else if (branch == 'N') {
            fault = snapshot.overcurrent_fault2 || snapshot.overvoltage_fault2;
        } else {
            uart_puts(UART_ID, "ERR OUT\r\n");
            return;
        }
        if (turn_on && fault) {
            uart_puts(UART_ID, "ERR FAULT\r\n");
            return;
        }
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        if (branch == 'P') {
            control_settings.output_enabled = turn_on;
        } else {
            control_settings.output_enabled2 = turn_on;
        }
        xSemaphoreGive(state_mutex);
        if (!turn_on && branch == 'P') {
            pwm_set_duty_safe(0.0f);
        } else if (!turn_on) {
            pwm_set_duty2_safe(0.0f);
        }
        snprintf(response, sizeof(response), "OK OUT %c %s\r\n", branch, state);
        uart_puts(UART_ID, response);
        return;
    }

    if (strcmp(command, "SAVE") == 0) {
        if (snapshot.output_enabled || snapshot.output_enabled2) {
            uart_puts(UART_ID, "ERR OUTPUT ON\r\n");
            return;
        }
        stored_settings_t settings = {.voltage_positive = snapshot.voltage_setpoint, .voltage_negative = snapshot.voltage_setpoint2, .current_positive = snapshot.overcurrent_limit, .current_negative = snapshot.overcurrent_limit2, .overvoltage_positive = snapshot.overvoltage_limit, .overvoltage_negative = snapshot.overvoltage_limit2};
        settings_storage_save(&settings);
        uart_puts(UART_ID, "OK SAVED\r\n");
        return;
    }

    uart_puts(UART_ID, "ERR COMMAND\r\n");
}

// =====================================================
// Tareas de comunicación e interfaz
// =====================================================

static void uart_task(void *parameters) {
    (void)parameters;
    char command[48];
    size_t length = 0U;

    while (true) {
        if (uart_is_readable(UART_ID)) {
            char received = uart_getc(UART_ID);
            if (received == '\n') {
                command[length] = '\0';
                if (length > 0U) {
                    uart_process_command(command);
                }
                length = 0U;
            } else if (received != '\r' && length < sizeof(command) - 1U) {
                command[length] = received;
                length++;
            }
        }
        vTaskDelay(milliseconds_to_ticks_min(UART_PERIOD_MS));
    }
}

static void display_task(void *parameters) {
    (void)parameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period_ticks = milliseconds_to_ticks_min(DISPLAY_UPDATE_MS);
    while (true) {
        system_snapshot_t snapshot = get_system_snapshot();
        xSemaphoreTake(i2c_mutex, portMAX_DELAY);
        lcd_show_values(&snapshot);
        xSemaphoreGive(i2c_mutex);
        printf("Vi=%.3f V | Ref1=%.2f V Ref2=-%.2f V | D1=%.2f%% D2=%.2f%% | Vo1=%.3f V Vo2=-%.3f V | Io1=%.3f A Io2=%.3f A | EN1=%u EN2=%u OV1=%u OV2=%u OC1=%u OC2=%u | IL1=%.2f IL2=%.2f VL1=%.1f VL2=%.1f M1=%u M2=%u | ADS=%u,%u,%u,%u\n", snapshot.measurements.vin, snapshot.voltage_setpoint, snapshot.voltage_setpoint2, snapshot.duty_percent, snapshot.duty_percent2, snapshot.measurements.vout, snapshot.measurements.vout2, snapshot.measurements.iout, snapshot.measurements.iout2, (unsigned int)snapshot.output_enabled, (unsigned int)snapshot.output_enabled2, (unsigned int)snapshot.overvoltage_fault, (unsigned int)snapshot.overvoltage_fault2, (unsigned int)snapshot.overcurrent_fault, (unsigned int)snapshot.overcurrent_fault2, snapshot.overcurrent_limit, snapshot.overcurrent_limit2, snapshot.overvoltage_limit, snapshot.overvoltage_limit2, snapshot.edit_mode, snapshot.edit_mode2, snapshot.measurements.vout_raw, snapshot.measurements.vout2_raw, snapshot.measurements.iout_raw, snapshot.measurements.iout2_raw);
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

    stored_settings_t saved_settings;
    if (settings_storage_load(&saved_settings)) {
        runtime_settings.overvoltage_limit = clamp_float(saved_settings.overvoltage_positive, OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
        runtime_settings.overvoltage_limit2 = clamp_float(saved_settings.overvoltage_negative, OVERVOLTAGE_LIMIT_MIN_VOLTS, VOUT_OVERVOLTAGE_TRIP);
        runtime_settings.overcurrent_limit = clamp_float(saved_settings.current_positive, CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
        runtime_settings.overcurrent_limit2 = clamp_float(saved_settings.current_negative, CURRENT_LIMIT_MIN_AMPS, CURRENT_LIMIT_MAX_AMPS);
        control_settings.voltage_setpoint = clamp_float(saved_settings.voltage_positive, VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit);
        control_settings.voltage_setpoint2 = clamp_float(saved_settings.voltage_negative, VOLTAGE_SETPOINT_MIN, runtime_settings.overvoltage_limit2);
    }

    adc_init();
    adc_gpio_init(VIN_ADC_GPIO);
    adc_gpio_init(IOUT2_ADC_GPIO);
    adc_gpio_init(IOUT1_ADC_GPIO);
    i2c_init(I2C_PORT, 100U * 1000U);
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_GPIO);
    gpio_pull_up(SCL_GPIO);
    i2c_mutex = xSemaphoreCreateMutex();
    state_mutex = xSemaphoreCreateMutex();
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
    pwm_generator_init();
    adc_fifo_protection_init();
    sleep_ms(1000);
    lcd_clear();
    xTaskCreate(adc_task, "ADC", TASK_STACK_ADC_WORDS, NULL, TASK_PRIORITY_ADC, NULL);
    xTaskCreate(control_task, "CONTROL", TASK_STACK_CONTROL_WORDS, NULL, TASK_PRIORITY_CONTROL, NULL);
    xTaskCreate(control_task, "CONTROL2", TASK_STACK_CONTROL2_WORDS, (void *)1, TASK_PRIORITY_CONTROL2, NULL);
    xTaskCreate(encoder_task, "ENCODER", TASK_STACK_ENCODER_WORDS, NULL, TASK_PRIORITY_ENCODER, NULL);
    xTaskCreate(uart_task, "UART", TASK_STACK_UART_WORDS, NULL, TASK_PRIORITY_UART, NULL);
    xTaskCreate(display_task, "DISPLAY", TASK_STACK_DISPLAY_WORDS, NULL, TASK_PRIORITY_DISPLAY, NULL);
    vTaskStartScheduler();
    panic("El scheduler se detuvo");
    return 0;
}

// =====================================================
// Hooks FreeRTOS
// =====================================================

void vApplicationMallocFailedHook(void) {
    pwm_set_duty_safe(0.0f);
    pwm_set_duty2_safe(0.0f);
    panic("FreeRTOS malloc failed");
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name) {
    (void)task;
    pwm_set_duty_safe(0.0f);
    pwm_set_duty2_safe(0.0f);
    panic("FreeRTOS stack overflow en tarea: %s", task_name);
}
