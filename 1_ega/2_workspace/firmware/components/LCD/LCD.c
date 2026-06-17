#include "LCD.h"
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdarg.h>

static const char *TAG = "LCD";

#define LCD_RS  0x01
#define LCD_RW  0x02
#define LCD_EN  0x04
#define LCD_BL  0x08

static esp_err_t i2c_write(lcd_handle_t *lcd, uint8_t data)
{
    esp_err_t ret;
    for (int i = 0; i < 3; i++) {
        ret = i2c_bus_write(lcd->addr, &data, 1);
        if (ret == ESP_OK) return ESP_OK;
        esp_rom_delay_us(100);
    }
    ESP_LOGE(TAG, "i2c_write(0x%02X) err=0x%X", data, ret);
    return ret;
}

static esp_err_t pulse_enable(lcd_handle_t *lcd, uint8_t data)
{
    esp_err_t ret;
    ret = i2c_write(lcd, data | LCD_EN);
    if (ret != ESP_OK) return ret;
    esp_rom_delay_us(1);
    ret = i2c_write(lcd, data);
    if (ret != ESP_OK) return ret;
    esp_rom_delay_us(50);
    return ESP_OK;
}

static esp_err_t write_nibble(lcd_handle_t *lcd, uint8_t nibble, uint8_t flags)
{
    return pulse_enable(lcd, (nibble & 0xF0) | lcd->backlight | flags);
}

static esp_err_t write_byte(lcd_handle_t *lcd, uint8_t byte, uint8_t flags)
{
    xSemaphoreTake(lcd->mutex, portMAX_DELAY);
    esp_err_t ret = write_nibble(lcd, byte & 0xF0, flags);
    if (ret == ESP_OK) ret = write_nibble(lcd, (uint8_t)(byte << 4), flags);
    xSemaphoreGive(lcd->mutex);
    return ret;
}

esp_err_t lcd_send_cmd(lcd_handle_t *lcd, uint8_t cmd)
{
    return write_byte(lcd, cmd, 0);
}

esp_err_t lcd_send_data(lcd_handle_t *lcd, uint8_t data)
{
    return write_byte(lcd, data, LCD_RS);
}

esp_err_t lcd_init(uint8_t addr, lcd_handle_t *out_lcd)
{
    out_lcd->addr = addr;

    out_lcd->mutex = xSemaphoreCreateMutex();
    if (!out_lcd->mutex) return ESP_ERR_NO_MEM;

    out_lcd->backlight    = LCD_BL;
    out_lcd->display_ctrl = 0x0C;

    vTaskDelay(pdMS_TO_TICKS(100));

    write_nibble(out_lcd, 0x30, 0);
    esp_rom_delay_us(4500);
    write_nibble(out_lcd, 0x30, 0);
    esp_rom_delay_us(200);
    write_nibble(out_lcd, 0x30, 0);
    esp_rom_delay_us(200);
    write_nibble(out_lcd, 0x20, 0);
    esp_rom_delay_us(200);

    lcd_send_cmd(out_lcd, 0x28);
    esp_rom_delay_us(80);
    lcd_send_cmd(out_lcd, 0x08);
    esp_rom_delay_us(80);
    lcd_send_cmd(out_lcd, 0x01);
    esp_rom_delay_us(2000);
    lcd_send_cmd(out_lcd, 0x06);
    esp_rom_delay_us(80);
    lcd_send_cmd(out_lcd, out_lcd->display_ctrl);
    esp_rom_delay_us(80);

    return ESP_OK;
}

void lcd_clear(lcd_handle_t *lcd)
{
    lcd_send_cmd(lcd, 0x01);
    esp_rom_delay_us(2000);
}

void lcd_home(lcd_handle_t *lcd)
{
    lcd_send_cmd(lcd, 0x02);
    esp_rom_delay_us(2000);
}

void lcd_set_cursor(lcd_handle_t *lcd, uint8_t col, uint8_t row)
{
    static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
    lcd_send_cmd(lcd, 0x80 | (col + row_offsets[row % 4]));
}

static void apply_display_ctrl(lcd_handle_t *lcd)
{
    lcd_send_cmd(lcd, lcd->display_ctrl);
}

void lcd_display_on(lcd_handle_t *lcd)  { lcd->display_ctrl |=  0x04; apply_display_ctrl(lcd); }
void lcd_display_off(lcd_handle_t *lcd) { lcd->display_ctrl &= ~0x04; apply_display_ctrl(lcd); }
void lcd_cursor_on(lcd_handle_t *lcd)   { lcd->display_ctrl |=  0x02; apply_display_ctrl(lcd); }
void lcd_cursor_off(lcd_handle_t *lcd)  { lcd->display_ctrl &= ~0x02; apply_display_ctrl(lcd); }
void lcd_blink_on(lcd_handle_t *lcd)    { lcd->display_ctrl |=  0x01; apply_display_ctrl(lcd); }
void lcd_blink_off(lcd_handle_t *lcd)   { lcd->display_ctrl &= ~0x01; apply_display_ctrl(lcd); }

void lcd_backlight_on(lcd_handle_t *lcd)
{
    lcd->backlight = LCD_BL;
    i2c_write(lcd, LCD_BL);
}

void lcd_backlight_off(lcd_handle_t *lcd)
{
    lcd->backlight = 0;
    i2c_write(lcd, 0x00);
}

void lcd_print_char(lcd_handle_t *lcd, char c)
{
    lcd_send_data(lcd, (uint8_t)c);
}

void lcd_print(lcd_handle_t *lcd, const char *str)
{
    while (*str) lcd_print_char(lcd, *str++);
}

void lcd_printf(lcd_handle_t *lcd, const char *fmt, ...)
{
    char buf[21];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lcd_print(lcd, buf);
}

void lcd_create_char(lcd_handle_t *lcd, uint8_t location, const uint8_t charmap[8])
{
    location &= 0x07;
    lcd_send_cmd(lcd, 0x40 | (location << 3));
    for (int i = 0; i < 8; i++) {
        lcd_send_data(lcd, charmap[i]);
    }
    lcd_send_cmd(lcd, 0x80);
}
