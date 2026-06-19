#include "driver/i2c.h"
#include "esp_log.h"

// Define el puerto I2C0
#define I2C_MASTER_NUM  I2C_NUM_0 

// La dirección I2C del módulo
#define MCP4725_ADDR    0x60      

static const char *TAG = "MCP4725";

/**
 * @brief Envía un valor digital de 12 bits al DAC MCP4725
 * @param value Valor entre 0 y 4095
 * @return esp_err_t ESP_OK si fue exitoso, o el código de error I2C
 */
esp_err_t mcp4725_set_voltage(uint16_t value) {
    
    // 1. Protección: Forzamos a que el valor no supere los 12 bits (4095)
    if (value > 4095) {
        value = 4095;
    }

    // 2. Preparar los 2 bytes para el "Fast Mode" del MCP4725
    // Byte 1: 0 0 0 0 (Control) + D11 D10 D9 D8 (4 bits más significativos)
    // Byte 2: D7 D6 D5 D4 D3 D2 D1 D0 (8 bits menos significativos)
    uint8_t data[2];
    data[0] = (uint8_t)((value >> 8) & 0x0F); 
    data[1] = (uint8_t)(value & 0xFF);

    // 3. Transmitir por el bus I2C
    esp_err_t err = i2c_master_write_to_device(
        I2C_MASTER_NUM,         // Puerto I2C
        MCP4725_ADDR,           // Dirección del esclavo
        data,                   // Nuestro arreglo de 2 bytes
        sizeof(data),           // Tamaño (2)
        pdMS_TO_TICKS(100)      // Timeout de 100ms
    );

    // 4. Manejo de errores
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al comunicar con DAC: %s", esp_err_to_name(err));
    }

    return err;
}