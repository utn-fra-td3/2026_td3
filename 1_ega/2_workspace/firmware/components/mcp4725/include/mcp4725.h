#pragma once

#include <stdint.h>
#include <esp_err.h>

#define MCP4725_ADDR_A0_GND  0x60
#define MCP4725_ADDR_A0_VCC  0x61
#define MCP4725_MAX_VALUE    0x0FFF

typedef struct {
    uint8_t addr;
} mcp4725_t;

esp_err_t mcp4725_init(mcp4725_t *dev, uint8_t addr);
esp_err_t mcp4725_set_raw(mcp4725_t *dev, uint16_t value);
esp_err_t mcp4725_set_voltage(mcp4725_t *dev, float vdd, float voltage);
