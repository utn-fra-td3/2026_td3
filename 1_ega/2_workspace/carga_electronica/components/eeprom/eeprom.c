#include "eeprom.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EEPROM";
static i2c_master_dev_handle_t eeprom_dev_handle = NULL;

// CONFIGURACIÓN ESPECÍFICA MÓDULO DS3231

// Direccion AT24C32 0x57
#define EEPROM_I2C_ADDR   0x57 
#define EEPROM_SDA_PIN    8
#define EEPROM_SCL_PIN    9
#define MAGIC_NUMBER      0x45474131 // Código "EGA1" para validar la memoria

// Importo handle I2C de main.c
extern i2c_master_bus_handle_t i2c_bus_handle;

typedef struct {
    uint32_t magic;
    uint32_t mode;
    float setpoint_cc;
    float setpoint_cr;
} eeprom_data_t;

void eeprom_init(void) {
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EEPROM_I2C_ADDR,
        .scl_speed_hz = 100000, 
    };
    
    // Uso 'i2c_bus_handle'
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &eeprom_dev_handle));
    ESP_LOGI(TAG, "Memoria EEPROM enlazada al bus global (Dirección: 0x%02X).", EEPROM_I2C_ADDR);
}

void eeprom_save_config(system_mode_t mode, float setpoint_cc, float setpoint_cr) {
    if (eeprom_dev_handle == NULL) return;

    eeprom_data_t data = {
        .magic = MAGIC_NUMBER,
        .mode = (uint32_t)mode,
        .setpoint_cc = setpoint_cc,
        .setpoint_cr = setpoint_cr
    };

    // Protocolo AT24C32: 2 bytes de dirección interna (0x0000) + los datos
    uint8_t write_buf[2 + sizeof(eeprom_data_t)];
    write_buf[0] = 0x00; 
    write_buf[1] = 0x00; 
    memcpy(&write_buf[2], &data, sizeof(eeprom_data_t));

    esp_err_t err = i2c_master_transmit(eeprom_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Guardado no volátil: Modo=%d | CC=%.2fA | CR=%.0fR", mode, setpoint_cc, setpoint_cr);
    } else {
        ESP_LOGE(TAG, "Fallo al escribir EEPROM: %s", esp_err_to_name(err));
    }
    // Tiempo físico requerido por el chip para terminar de grabar internamente
    vTaskDelay(pdMS_TO_TICKS(10));
}

bool eeprom_load_config(system_mode_t *mode, float *setpoint_cc, float *setpoint_cr) {
    if (eeprom_dev_handle == NULL) return false;

    uint8_t addr_buf[2] = {0x00, 0x00};
    eeprom_data_t data;

    esp_err_t err = i2c_master_transmit_receive(eeprom_dev_handle, addr_buf, sizeof(addr_buf), (uint8_t *)&data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error de lectura EEPROM: %s", esp_err_to_name(err));
        return false;
    }

    if (data.magic == MAGIC_NUMBER) {
        *mode = (system_mode_t)data.mode;
        *setpoint_cc = data.setpoint_cc;
        *setpoint_cr = data.setpoint_cr;
        return true;
    }
    return false; // Memoria conectada pero vacía
}