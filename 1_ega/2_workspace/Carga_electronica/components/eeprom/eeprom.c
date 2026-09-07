#include "eeprom.h"

#define I2C_MASTER_NUM      I2C_NUM_0
#define EEPROM_I2C_ADDR     0x57      // Dirección de la eeprom

/**
 * @brief Escribe un único byte en una dirección específica de la EEPROM
 */
esp_err_t eeprom_write_byte(uint16_t mem_addr, uint8_t data) {
    uint8_t write_buf[3];
    
    // Al ser una memoria de 32Kbits, la dirección interna usa 2 bytes (MSB y LSB)
    write_buf[0] = (uint8_t)(mem_addr >> 8);   // Byte alto de la dirección
    write_buf[1] = (uint8_t)(mem_addr & 0xFF); // Byte bajo de la dirección
    write_buf[2] = data;                       // El dato que queremos guardar

    esp_err_t err = i2c_master_write_to_device(
        I2C_MASTER_NUM, EEPROM_I2C_ADDR, 
        write_buf, sizeof(write_buf), pdMS_TO_TICKS(100)
    );

    // La documentacion exige esperar entre 5ms y 10ms para guardar un rato. Esto protege la eeprom de escrituras muy seguidas
    vTaskDelay(pdMS_TO_TICKS(10)); 

    return err;
}

/**
 * @brief Lee un único byte de una dirección específica de la EEPROM
 */
esp_err_t eeprom_read_byte(uint16_t mem_addr, uint8_t *data) {
    uint8_t addr_buf[2];
    addr_buf[0] = (uint8_t)(mem_addr >> 8);
    addr_buf[1] = (uint8_t)(mem_addr & 0xFF);

    // Mandamos los 2 bytes de la dirección que queremos leer, 
    // y luego el bus cambia a modo lectura para recibir el byte en la variable data
    return i2c_master_write_read_device(
        I2C_MASTER_NUM, EEPROM_I2C_ADDR, 
        addr_buf, sizeof(addr_buf), 
        data, 1, pdMS_TO_TICKS(100)
    );
}
