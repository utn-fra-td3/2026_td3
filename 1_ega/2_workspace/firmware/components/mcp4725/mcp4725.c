#include "mcp4725.h"
#include "i2c_bus.h"

#define CMD_WRITE_DAC  0x40

esp_err_t mcp4725_init(mcp4725_t *dev, uint8_t addr)
{
    dev->addr = addr;
    return ESP_OK;
}

esp_err_t mcp4725_set_raw(mcp4725_t *dev, uint16_t value)
{
    if (value > MCP4725_MAX_VALUE) value = MCP4725_MAX_VALUE;
    uint8_t buf[3] = {
        CMD_WRITE_DAC,
        (value >> 4) & 0xFF,
        (value & 0x0F) << 4,
    };
    return i2c_bus_write(dev->addr, buf, sizeof(buf));
}

esp_err_t mcp4725_set_voltage(mcp4725_t *dev, float vdd, float voltage)
{
    if (voltage < 0)   voltage = 0;
    if (voltage > vdd) voltage = vdd;
    uint16_t raw = (uint16_t)((voltage / vdd) * MCP4725_MAX_VALUE);
    return mcp4725_set_raw(dev, raw);
}
