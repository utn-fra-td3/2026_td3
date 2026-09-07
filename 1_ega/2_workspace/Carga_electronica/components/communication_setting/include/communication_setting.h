#ifndef COMMUNICATION_SETTING_H
#define COMMUNICATION_SETTING_H

#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// CONFIGURACIÓN DE COMUNICACIÓN (SPI, I2C)
// ==========================================

// --- I2C (EEPROM / DAC) ---
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_SDA_IO           8
#define I2C_MASTER_SCL_IO           9
#define I2C_MASTER_FREQ_HZ          100000   // 100kHz para evitar ruido en protoboard

// // --- SPI (DISPLAY TFT) ---
#define LCD_HOST                    SPI2_HOST
#define PIN_NUM_CS                  10
#define PIN_NUM_RESET               13
#define PIN_NUM_DC                  14
#define PIN_NUM_MOSI                12
#define PIN_NUM_SCLK                11
#define PIN_NUM_LED                 21
//#define PIN_NUM_MISO                5


esp_err_t communication_i2c_init(void);
esp_err_t communication_spi_init(void);

#ifdef __cplusplus
}
#endif

#endif // COMMUNICATION_SETTING_H
