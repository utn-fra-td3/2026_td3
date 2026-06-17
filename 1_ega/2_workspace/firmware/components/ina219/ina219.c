#include <esp_log.h>
#include <math.h>
#include "ina219.h"
#include "i2c_bus.h"

static const char *TAG = "ina219";

#define REG_CONFIG      0
#define REG_SHUNT_U     1
#define REG_BUS_U       2
#define REG_POWER       3
#define REG_CURRENT     4
#define REG_CALIBRATION 5

#define BIT_RST   15
#define BIT_BRNG  13
#define BIT_PG0   11
#define BIT_BADC0 7
#define BIT_SADC0 3
#define BIT_MODE  0

#define MASK_PG   (3 << BIT_PG0)
#define MASK_BADC (0xf << BIT_BADC0)
#define MASK_SADC (0xf << BIT_SADC0)
#define MASK_MODE (7 << BIT_MODE)
#define MASK_BRNG (1 << BIT_BRNG)

#define DEF_CONFIG 0x399f

#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

static const float u_shunt_max[] = {
    [INA219_GAIN_1]     = 0.04,
    [INA219_GAIN_0_5]   = 0.08,
    [INA219_GAIN_0_25]  = 0.16,
    [INA219_GAIN_0_125] = 0.32,
};

static esp_err_t read_reg_16(ina219_t *dev, uint8_t reg, uint16_t *val)
{
    CHECK_ARG(val);
    uint8_t buf[2];
    CHECK(i2c_bus_write_read(dev->addr, &reg, 1, buf, 2));
    *val = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}

static esp_err_t write_reg_16(ina219_t *dev, uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = { reg, val >> 8, val & 0xFF };
    return i2c_bus_write(dev->addr, buf, sizeof(buf));
}

static esp_err_t read_conf_bits(ina219_t *dev, uint16_t mask, uint8_t bit, uint16_t *res)
{
    uint16_t raw;
    CHECK(read_reg_16(dev, REG_CONFIG, &raw));
    *res = (raw & mask) >> bit;
    return ESP_OK;
}

///////////////////////////////////////////////////////////////////////////////

esp_err_t ina219_init(ina219_t *dev, uint8_t addr)
{
    CHECK_ARG(dev);

    if (addr < INA219_ADDR_GND_GND || addr > INA219_ADDR_SCL_SCL) {
        ESP_LOGE(TAG, "Invalid I2C address");
        return ESP_ERR_INVALID_ARG;
    }

    dev->addr = addr;
    CHECK(read_reg_16(dev, REG_CONFIG, &dev->config));
    ESP_LOGD(TAG, "Initialize, config: 0x%04x", dev->config);
    return ESP_OK;
}

esp_err_t ina219_reset(ina219_t *dev)
{
    CHECK_ARG(dev);
    CHECK(write_reg_16(dev, REG_CONFIG, 1 << BIT_RST));
    dev->config = DEF_CONFIG;
    ESP_LOGD(TAG, "Device reset");
    return ESP_OK;
}

esp_err_t ina219_configure(ina219_t *dev, ina219_bus_voltage_range_t u_range,
                           ina219_gain_t gain, ina219_resolution_t u_res,
                           ina219_resolution_t i_res, ina219_mode_t mode)
{
    CHECK_ARG(dev);
    CHECK_ARG(u_range <= INA219_BUS_RANGE_32V);
    CHECK_ARG(gain <= INA219_GAIN_0_125);
    CHECK_ARG(u_res <= INA219_RES_12BIT_128S);
    CHECK_ARG(i_res <= INA219_RES_12BIT_128S);
    CHECK_ARG(mode <= INA219_MODE_CONT_SHUNT_BUS);

    dev->config = (u_range << BIT_BRNG) |
                  (gain << BIT_PG0) |
                  (u_res << BIT_BADC0) |
                  (i_res << BIT_SADC0) |
                  (mode << BIT_MODE);

    ESP_LOGD(TAG, "Config: 0x%04x", dev->config);
    return write_reg_16(dev, REG_CONFIG, dev->config);
}

esp_err_t ina219_get_bus_voltage_range(ina219_t *dev, ina219_bus_voltage_range_t *range)
{
    CHECK_ARG(dev && range);
    *range = 0;
    return read_conf_bits(dev, MASK_BRNG, BIT_BRNG, (uint16_t *)range);
}

esp_err_t ina219_get_gain(ina219_t *dev, ina219_gain_t *gain)
{
    CHECK_ARG(dev && gain);
    *gain = 0;
    return read_conf_bits(dev, MASK_PG, BIT_PG0, (uint16_t *)gain);
}

esp_err_t ina219_get_bus_voltage_resolution(ina219_t *dev, ina219_resolution_t *res)
{
    CHECK_ARG(dev && res);
    *res = 0;
    return read_conf_bits(dev, MASK_BADC, BIT_BADC0, (uint16_t *)res);
}

esp_err_t ina219_get_shunt_voltage_resolution(ina219_t *dev, ina219_resolution_t *res)
{
    CHECK_ARG(dev && res);
    *res = 0;
    return read_conf_bits(dev, MASK_SADC, BIT_SADC0, (uint16_t *)res);
}

esp_err_t ina219_get_mode(ina219_t *dev, ina219_mode_t *mode)
{
    CHECK_ARG(dev && mode);
    *mode = 0;
    return read_conf_bits(dev, MASK_MODE, BIT_MODE, (uint16_t *)mode);
}

esp_err_t ina219_calibrate(ina219_t *dev, float r_shunt)
{
    CHECK_ARG(dev);

    ina219_gain_t gain;
    CHECK(ina219_get_gain(dev, &gain));

    dev->i_lsb = (uint16_t)(u_shunt_max[gain] / r_shunt / 32767 * 100000000);
    dev->i_lsb /= 100000000;
    dev->i_lsb /= 0.0001;
    dev->i_lsb = ceil(dev->i_lsb);
    dev->i_lsb *= 0.0001;

    dev->p_lsb = dev->i_lsb * 20;

    uint16_t cal = (uint16_t)((0.04096) / (dev->i_lsb * r_shunt));

    ESP_LOGD(TAG, "Calibration: %.04f Ohm, 0x%04x", r_shunt, cal);
    return write_reg_16(dev, REG_CALIBRATION, cal);
}

esp_err_t ina219_trigger(ina219_t *dev)
{
    CHECK_ARG(dev);

    uint16_t mode = (dev->config & MASK_MODE) >> BIT_MODE;
    if (mode < INA219_MODE_TRIG_SHUNT || mode > INA219_MODE_TRIG_SHUNT_BUS) {
        ESP_LOGE(TAG, "Could not trigger conversion in this mode: %d", mode);
        return ESP_ERR_INVALID_STATE;
    }

    return write_reg_16(dev, REG_CONFIG, dev->config);
}

esp_err_t ina219_get_bus_voltage(ina219_t *dev, float *voltage)
{
    CHECK_ARG(dev && voltage);
    uint16_t raw;
    CHECK(read_reg_16(dev, REG_BUS_U, &raw));
    *voltage = (raw >> 3) * 0.004;
    return ESP_OK;
}

esp_err_t ina219_get_shunt_voltage(ina219_t *dev, float *voltage)
{
    CHECK_ARG(dev && voltage);
    int16_t raw;
    CHECK(read_reg_16(dev, REG_SHUNT_U, (uint16_t *)&raw));
    *voltage = raw / 100000.0;
    return ESP_OK;
}

esp_err_t ina219_get_current(ina219_t *dev, float *current)
{
    CHECK_ARG(dev && current);
    int16_t raw;
    CHECK(read_reg_16(dev, REG_CURRENT, (uint16_t *)&raw));
    *current = raw * dev->i_lsb;
    return ESP_OK;
}

esp_err_t ina219_get_power(ina219_t *dev, float *power)
{
    CHECK_ARG(dev && power);
    int16_t raw;
    CHECK(read_reg_16(dev, REG_POWER, (uint16_t *)&raw));
    *power = raw * dev->p_lsb;
    return ESP_OK;
}
