#include "ads1115.h"

#include <stddef.h>

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define ADS1115_REG_CONVERSION 0x00U
#define ADS1115_REG_CONFIG 0x01U

#define ADS1115_CONFIG_START_SINGLE 0x8000U
#define ADS1115_CONFIG_SINGLE_ENDED 0x4000U
#define ADS1115_CONFIG_SINGLE_SHOT 0x0100U
#define ADS1115_CONFIG_DISABLE_COMPARATOR 0x0003U

static bool ads1115_address_is_valid(uint8_t address) {
    return address >= ADS1115_ADDRESS_GND && address <= ADS1115_ADDRESS_SCL;
}

static bool ads1115_gain_is_valid(ads1115_gain_t gain) {
    return gain >= ADS1115_GAIN_6_144V && gain <= ADS1115_GAIN_0_256V;
}

static bool ads1115_data_rate_is_valid(ads1115_data_rate_t data_rate) {
    return data_rate >= ADS1115_DATA_RATE_8_SPS && data_rate <= ADS1115_DATA_RATE_860_SPS;
}

static uint16_t ads1115_gain_bits(ads1115_gain_t gain) {
    return (uint16_t)gain << 9;
}

static uint16_t ads1115_data_rate_bits(ads1115_data_rate_t data_rate) {
    return (uint16_t)data_rate << 5;
}

static uint16_t ads1115_samples_per_second(ads1115_data_rate_t data_rate) {
    static const uint16_t rates[] = {8U, 16U, 32U, 64U, 128U, 250U, 475U, 860U};
    return rates[(uint8_t)data_rate];
}

static float ads1115_full_scale_voltage(ads1115_gain_t gain) {
    static const float full_scale_volts[] = {6.144f, 4.096f, 2.048f, 1.024f, 0.512f, 0.256f};
    return full_scale_volts[(uint8_t)gain];
}

bool ads1115_init(ads1115_t *device, i2c_inst_t *i2c, uint8_t address, ads1115_gain_t gain, ads1115_data_rate_t data_rate) {
    if (device == NULL || i2c == NULL || !ads1115_address_is_valid(address) || !ads1115_gain_is_valid(gain) || !ads1115_data_rate_is_valid(data_rate)) return false;
    device->i2c = i2c;
    device->address = address;
    device->gain = gain;
    device->data_rate = data_rate;
    return true;
}

bool ads1115_start_single_ended(const ads1115_t *device, uint8_t channel) {
    if (device == NULL || device->i2c == NULL || channel > 3U) return false;
    uint16_t config = ADS1115_CONFIG_START_SINGLE;
    config |= (uint16_t)(ADS1115_CONFIG_SINGLE_ENDED | ((uint16_t)channel << 12));
    config |= ads1115_gain_bits(device->gain);
    config |= ADS1115_CONFIG_SINGLE_SHOT;
    config |= ads1115_data_rate_bits(device->data_rate);
    config |= ADS1115_CONFIG_DISABLE_COMPARATOR;
    uint8_t buffer[3] = {ADS1115_REG_CONFIG, (uint8_t)(config >> 8), (uint8_t)(config & 0xFFU)};
    return i2c_write_blocking(device->i2c, device->address, buffer, 3, false) == 3;
}

bool ads1115_read_conversion(const ads1115_t *device, int16_t *raw_result) {
    if (device == NULL || device->i2c == NULL || raw_result == NULL) return false;
    uint8_t register_address = ADS1115_REG_CONVERSION;
    uint8_t received[2] = {0U, 0U};
    if (i2c_write_blocking(device->i2c, device->address, &register_address, 1, true) != 1) return false;
    if (i2c_read_blocking(device->i2c, device->address, received, 2, false) != 2) return false;
    *raw_result = (int16_t)(((uint16_t)received[0] << 8) | received[1]);
    return true;
}

bool ads1115_read_single_ended_blocking(const ads1115_t *device, uint8_t channel, uint16_t *raw_result) {
    if (raw_result == NULL || !ads1115_start_single_ended(device, channel)) return false;
    sleep_ms(ads1115_conversion_time_ms(device));
    int16_t signed_result = 0;
    if (!ads1115_read_conversion(device, &signed_result)) return false;
    *raw_result = signed_result < 0 ? 0U : (uint16_t)signed_result;
    return true;
}

uint32_t ads1115_conversion_time_ms(const ads1115_t *device) {
    if (device == NULL || !ads1115_data_rate_is_valid(device->data_rate)) return 0U;
    uint32_t samples_per_second = ads1115_samples_per_second(device->data_rate);
    return (1000U + samples_per_second - 1U) / samples_per_second;
}

float ads1115_raw_to_voltage(const ads1115_t *device, int16_t raw) {
    if (device == NULL || !ads1115_gain_is_valid(device->gain)) return 0.0f;
    return ((float)raw * ads1115_full_scale_voltage(device->gain)) / 32768.0f;
}

float ads1115_restore_divider_voltage(float pin_voltage, float upper_resistor_ohms, float lower_resistor_ohms) {
    if (pin_voltage <= 0.0f || upper_resistor_ohms < 0.0f || lower_resistor_ohms <= 0.0f) return 0.0f;
    return pin_voltage * (upper_resistor_ohms + lower_resistor_ohms) / lower_resistor_ohms;
}
