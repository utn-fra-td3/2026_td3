#include "lcd.h"

#define LCD_ENABLE_DELAY_US  1U
#define LCD_COMMAND_DELAY_US 50U
#define LCD_CLEAR_DELAY_US   2000U

// Puntero a I2C usado
static i2c_inst_t *lcd_i2c;
// Direccion de 7 bits del I2C
static uint8_t addr;

/**
 * @brief Manda un byte por I2C
 * @param val es el byte a mandar
*/
static void i2c_write_byte(uint8_t val) {
    i2c_write_blocking(lcd_i2c, addr, &val, 1, false);
}

/**
 * @brief Conmuta el enable del LCD para mandar el comando
 * @param val es el byte a enviar
*/
static void lcd_toggle_enable(uint8_t val) {
    sleep_us(LCD_ENABLE_DELAY_US);
    i2c_write_byte(val | LCD_ENABLE_BIT);
    sleep_us(LCD_ENABLE_DELAY_US);
    i2c_write_byte((uint8_t)(val & (uint8_t)(~LCD_ENABLE_BIT)));
    sleep_us(LCD_ENABLE_DELAY_US);
}

/**
 * @brief Envia el byte por I2C en dos nibbles
 * @param val es el byte a enviar
 * @param mode LCD_COMMAND si es un comando o LCD_CHARACTER si
 * es un caracter para escribir
*/
static void lcd_send_byte(uint8_t val, uint8_t mode) {
    uint8_t high = (uint8_t)(mode | (val & 0xF0U) | LCD_BACKLIGHT);
    uint8_t low = (uint8_t)(mode | ((val << 4) & 0xF0U) | LCD_BACKLIGHT);

    i2c_write_byte(high);
    lcd_toggle_enable(high);
    i2c_write_byte(low);
    lcd_toggle_enable(low);

    sleep_us(LCD_COMMAND_DELAY_US);
}

/**
 * @brief Envia un comando de limpiar y resetear cursor
*/
void lcd_clear(void) {
    lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND);
    sleep_us(LCD_CLEAR_DELAY_US);
}

/**
 * @brief Pone al cursor del LCD en la posicion indicada
 * @param line es el numero de linea (0 o 1)
 * @param position es el numero de caracter (0 a 15)
*/
void lcd_set_cursor(int line, int position) {
    int line_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    int val = 0x80 + line_offsets[line] + position;
    lcd_send_byte((uint8_t)val, LCD_COMMAND);
}

/**
 * @brief Escribe un caracter en el display
 * @param val es el caracter a enviar
*/
void lcd_char(char val) {
    lcd_send_byte(val, LCD_CHARACTER);
}

/**
 * @brief Escribe una cadena de caracteres en el display
 * @param s es la cadena a escribir
*/
void lcd_string(const char *s) {
    while (*s) {
        lcd_char(*s++);
    }
}

/**
 * @brief Inicializa el display
 * @param i2c puntero a I2C usado (i2c0 o i2c1)
 * @param address es la direccion de 7 bits del adaptador I2C
*/
void lcd_init(i2c_inst_t *i2c, uint8_t address) {
    // Guardo el I2C usado
    lcd_i2c = i2c;
    // Guardo la direccion del display
    addr = address;
    // Secuencia de arranque del HD44780 en modo de 4 bits.
    sleep_ms(50);
    lcd_send_byte(0x03, LCD_COMMAND);
    sleep_ms(5);
    lcd_send_byte(0x03, LCD_COMMAND);
    sleep_us(200);
    lcd_send_byte(0x03, LCD_COMMAND);
    sleep_us(200);
    lcd_send_byte(0x02, LCD_COMMAND);

    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND);
    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND);
    lcd_clear();
}
