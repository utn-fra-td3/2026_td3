#ifndef ADS1115_H
#define ADS1115_H

#include <stdbool.h>
#include <stdint.h>

typedef struct i2c_inst i2c_inst_t;

#define ADS1115_ADDRESS_GND 0x48U
#define ADS1115_ADDRESS_VDD 0x49U
#define ADS1115_ADDRESS_SDA 0x4AU
#define ADS1115_ADDRESS_SCL 0x4BU

typedef enum {
    ADS1115_GAIN_6_144V = 0,
    ADS1115_GAIN_4_096V,
    ADS1115_GAIN_2_048V,
    ADS1115_GAIN_1_024V,
    ADS1115_GAIN_0_512V,
    ADS1115_GAIN_0_256V
} ads1115_gain_t;

typedef enum {
    ADS1115_DATA_RATE_8_SPS = 0,
    ADS1115_DATA_RATE_16_SPS,
    ADS1115_DATA_RATE_32_SPS,
    ADS1115_DATA_RATE_64_SPS,
    ADS1115_DATA_RATE_128_SPS,
    ADS1115_DATA_RATE_250_SPS,
    ADS1115_DATA_RATE_475_SPS,
    ADS1115_DATA_RATE_860_SPS
} ads1115_data_rate_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t address;
    ads1115_gain_t gain;
    ads1115_data_rate_t data_rate;
} ads1115_t;

bool ads1115_init(ads1115_t *device, i2c_inst_t *i2c, uint8_t address, ads1115_gain_t gain, ads1115_data_rate_t data_rate);
bool ads1115_start_single_ended(const ads1115_t *device, uint8_t channel);
bool ads1115_read_conversion(const ads1115_t *device, int16_t *raw_result);
bool ads1115_read_single_ended_blocking(const ads1115_t *device, uint8_t channel, uint16_t *raw_result);
uint32_t ads1115_conversion_time_ms(const ads1115_t *device);
float ads1115_raw_to_voltage(const ads1115_t *device, int16_t raw);
float ads1115_restore_divider_voltage(float pin_voltage, float upper_resistor_ohms, float lower_resistor_ohms);

#endif
