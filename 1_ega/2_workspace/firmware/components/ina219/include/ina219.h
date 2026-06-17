#ifndef __INA219_H__
#define __INA219_H__

#include <esp_err.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INA219_ADDR_GND_GND 0x40
#define INA219_ADDR_GND_VS  0x41
#define INA219_ADDR_GND_SDA 0x42
#define INA219_ADDR_GND_SCL 0x43
#define INA219_ADDR_VS_GND  0x44
#define INA219_ADDR_VS_VS   0x45
#define INA219_ADDR_VS_SDA  0x46
#define INA219_ADDR_VS_SCL  0x47
#define INA219_ADDR_SDA_GND 0x48
#define INA219_ADDR_SDA_VS  0x49
#define INA219_ADDR_SDA_SDA 0x4a
#define INA219_ADDR_SDA_SCL 0x4b
#define INA219_ADDR_SCL_GND 0x4c
#define INA219_ADDR_SCL_VS  0x4d
#define INA219_ADDR_SCL_SDA 0x4e
#define INA219_ADDR_SCL_SCL 0x4f

typedef enum {
    INA219_BUS_RANGE_16V = 0,
    INA219_BUS_RANGE_32V
} ina219_bus_voltage_range_t;

typedef enum {
    INA219_GAIN_1 = 0,
    INA219_GAIN_0_5,
    INA219_GAIN_0_25,
    INA219_GAIN_0_125
} ina219_gain_t;

typedef enum {
    INA219_RES_9BIT_1S    = 0,
    INA219_RES_10BIT_1S   = 1,
    INA219_RES_11BIT_1S   = 2,
    INA219_RES_12BIT_1S   = 3,
    INA219_RES_12BIT_2S   = 9,
    INA219_RES_12BIT_4S   = 10,
    INA219_RES_12BIT_8S   = 11,
    INA219_RES_12BIT_16S  = 12,
    INA219_RES_12BIT_32S  = 13,
    INA219_RES_12BIT_64S  = 14,
    INA219_RES_12BIT_128S = 15,
} ina219_resolution_t;

typedef enum {
    INA219_MODE_POWER_DOWN = 0,
    INA219_MODE_TRIG_SHUNT,
    INA219_MODE_TRIG_BUS,
    INA219_MODE_TRIG_SHUNT_BUS,
    INA219_MODE_DISABLED,
    INA219_MODE_CONT_SHUNT,
    INA219_MODE_CONT_BUS,
    INA219_MODE_CONT_SHUNT_BUS
} ina219_mode_t;

typedef struct {
    uint8_t  addr;
    uint16_t config;
    float    i_lsb, p_lsb;
} ina219_t;

esp_err_t ina219_init(ina219_t *dev, uint8_t addr);
esp_err_t ina219_reset(ina219_t *dev);

esp_err_t ina219_configure(ina219_t *dev, ina219_bus_voltage_range_t u_range,
                           ina219_gain_t gain, ina219_resolution_t u_res,
                           ina219_resolution_t i_res, ina219_mode_t mode);

esp_err_t ina219_calibrate(ina219_t *dev, float r_shunt);
esp_err_t ina219_trigger(ina219_t *dev);

esp_err_t ina219_get_bus_voltage(ina219_t *dev, float *voltage);
esp_err_t ina219_get_shunt_voltage(ina219_t *dev, float *voltage);
esp_err_t ina219_get_current(ina219_t *dev, float *current);
esp_err_t ina219_get_power(ina219_t *dev, float *power);

esp_err_t ina219_get_gain(ina219_t *dev, ina219_gain_t *gain);
esp_err_t ina219_get_bus_voltage_range(ina219_t *dev, ina219_bus_voltage_range_t *range);
esp_err_t ina219_get_bus_voltage_resolution(ina219_t *dev, ina219_resolution_t *res);
esp_err_t ina219_get_shunt_voltage_resolution(ina219_t *dev, ina219_resolution_t *res);
esp_err_t ina219_get_mode(ina219_t *dev, ina219_mode_t *mode);

#ifdef __cplusplus
}
#endif

#endif /* __INA219_H__ */
