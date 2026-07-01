#include "lcd_display.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "hd44780.h"

static const char *TAG = "LCD_DISPLAY";

static hd44780_t lcd = {
    .write_cb = NULL,
    .font = HD44780_FONT_5X8,
    .lines = LCD_ROWS,
    .pins = {
        .rs = 0,
        .e = 2,
        .d4 = 4,
        .d5 = 5,
        .d6 = 6,
        .d7 = 7,
        .bl = 3,
    },
};

static esp_err_t lcd_write_cb(const hd44780_t *lcd_desc, uint8_t data)
{
    (void)lcd_desc;

    // Callback usado por la libreria hd44780 para enviar cada byte al backpack
    // PCF8574. El bus ya esta arbitrado por I2C_Manager_Task.
    return i2c_master_write_to_device(
        I2C_PORT,
        LCD_I2C_ADDR,
        &data,
        1,
        pdMS_TO_TICKS(LCD_I2C_TIMEOUT_MS)
    );
}

static void lcd_format_line(char *dst, const char *src)
{
    size_t len = 0;

    // Cada linea del LCD se completa con espacios para borrar restos de textos
    // anteriores cuando el nuevo mensaje es mas corto.
    memset(dst, ' ', LCD_COLS);
    dst[LCD_COLS] = '\0';

    while (src[len] != '\0' && len < LCD_COLS) {
        dst[len] = src[len];
        len++;
    }
}

static esp_err_t lcd_write_line(uint8_t row, const char *text)
{
    char line[LCD_COLS + 1];

    lcd_format_line(line, text);

    ESP_RETURN_ON_ERROR(hd44780_gotoxy(&lcd, 0, row), TAG, "LCD gotoxy failed");
    ESP_RETURN_ON_ERROR(hd44780_puts(&lcd, line), TAG, "LCD puts failed");

    return ESP_OK;
}

esp_err_t lcd_display_write_screen(const char *line1, const char *line2)
{
    ESP_RETURN_ON_ERROR(lcd_write_line(0, line1), TAG, "LCD line 1 failed");
    ESP_RETURN_ON_ERROR(lcd_write_line(1, line2), TAG, "LCD line 2 failed");

    return ESP_OK;
}

esp_err_t lcd_display_init(void)
{
    // La inicializacion del LCD queda en este componente; la tarea Display_Task
    // solo envia mensajes logicos y no conoce detalles del hd44780.
    lcd.write_cb = lcd_write_cb;

    ESP_RETURN_ON_ERROR(hd44780_init(&lcd), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(hd44780_switch_backlight(&lcd, true), TAG, "LCD backlight failed");
    ESP_RETURN_ON_ERROR(hd44780_clear(&lcd), TAG, "LCD clear failed");
    ESP_RETURN_ON_ERROR(lcd_display_write_screen("TD3 PID", "Sistema iniciado"), TAG, "LCD splash failed");

    return ESP_OK;
}
