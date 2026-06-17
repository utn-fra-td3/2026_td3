#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include <stdint.h>

#define LCD_I2C_ADDR_DEFAULT  0x27

typedef struct {
    uint8_t             addr;
    SemaphoreHandle_t   mutex;
    uint8_t             backlight;
    uint8_t             display_ctrl;
} lcd_handle_t;

esp_err_t lcd_init(uint8_t addr, lcd_handle_t *out_lcd);

void lcd_clear(lcd_handle_t *lcd);
void lcd_home(lcd_handle_t *lcd);
void lcd_set_cursor(lcd_handle_t *lcd, uint8_t col, uint8_t row);

void lcd_print_char(lcd_handle_t *lcd, char c);
void lcd_print(lcd_handle_t *lcd, const char *str);
void lcd_printf(lcd_handle_t *lcd, const char *fmt, ...);

void lcd_backlight_on(lcd_handle_t *lcd);
void lcd_backlight_off(lcd_handle_t *lcd);

void lcd_display_on(lcd_handle_t *lcd);
void lcd_display_off(lcd_handle_t *lcd);
void lcd_cursor_on(lcd_handle_t *lcd);
void lcd_cursor_off(lcd_handle_t *lcd);
void lcd_blink_on(lcd_handle_t *lcd);
void lcd_blink_off(lcd_handle_t *lcd);

void lcd_create_char(lcd_handle_t *lcd, uint8_t location, const uint8_t charmap[8]);

esp_err_t lcd_send_cmd(lcd_handle_t *lcd, uint8_t cmd);
esp_err_t lcd_send_data(lcd_handle_t *lcd, uint8_t data);
