esp_err_t mcp4725_set_voltage(uint16_t value);

// Define el puerto I2C0
#define I2C_MASTER_NUM  I2C_NUM_0 

// La dirección I2C del módulo
#define MCP4725_ADDR    0x60      