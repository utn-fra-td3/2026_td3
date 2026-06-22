
#include "oled.h"



#define OLED_I2C_ADDR 0x3C

// --- COMANDOS BÁSICOS ---
esp_err_t oled_send_command(uint8_t command) {
    uint8_t buf[2] = {0x00, command}; 
    return i2c_master_write_to_device(I2C_NUM_0, OLED_I2C_ADDR, buf, 2, pdMS_TO_TICKS(10));
}

esp_err_t oled_send_data(uint8_t data) {
    uint8_t buf[2] = {0x40, data}; 
    return i2c_master_write_to_device(I2C_NUM_0, OLED_I2C_ADDR, buf, 2, pdMS_TO_TICKS(10));
}

// --- INICIALIZACIÓN ---
void oled_init_minimal(void) {
    oled_send_command(0xAE); // Display OFF
    oled_send_command(0x20); // Set Memory Addressing Mode
    oled_send_command(0x00); // Horizontal Addressing
    oled_send_command(0x81); // Set Contrast
    oled_send_command(0xFF); // Máximo contraste
    oled_send_command(0xA1); // Segment Re-map
    oled_send_command(0xA6); // Normal display
    oled_send_command(0x20); // Multiplex ratio
    oled_send_command(0x3F); // 1/64 duty
    oled_send_command(0x8D); // Charge Pump
    oled_send_command(0x14); // Enable Charge Pump
    oled_send_command(0xAF); // Display ON
}

// --- DIBUJO EN PANTALLA ---
void oled_set_cursor(uint8_t col, uint8_t page) {
    oled_send_command(0xB0 + page);       // Renglón (0 a 7)
    oled_send_command(col & 0x0F);        // Mitad baja columna
    oled_send_command(0x10 | (col >> 4)); // Mitad alta columna
}

void oled_clear(void) {
    for (uint8_t page = 0; page < 8; page++) {
        oled_set_cursor(0, page);
        for (uint8_t col = 0; col < 128; col++) {
            oled_send_data(0x00); // Escribimos píxeles apagados en toda la pantalla
        }
    }
}

// Matriz de píxeles: Números del 0 al 9
const uint8_t font_numbers[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}  // 9
};

// Imprimir un solo dígito
void oled_print_digit(uint8_t digit) {
    if (digit > 9) return;
    for (int i = 0; i < 5; i++) oled_send_data(font_numbers[digit][i]);
    oled_send_data(0x00); // Espacio entre números
}

// Imprimir un número completo 
void oled_print_number(uint16_t number) {
    // Extraemos cada dígito matemáticamente
    uint8_t miles = (number / 1000) % 10;
    uint8_t centenas = (number / 100) % 10;
    uint8_t decenas = (number / 10) % 10;
    uint8_t unidades = number % 10;

    // Solo imprimimos los miles si el número es >= 1000 (para evitar ceros a la izquierda)
    if (number >= 1000) oled_print_digit(miles);
    if (number >= 100)  oled_print_digit(centenas);
    if (number >= 10)   oled_print_digit(decenas);
    
    oled_print_digit(unidades); // Las unidades siempre se imprimen
}