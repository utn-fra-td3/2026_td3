#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lcd.h"
#include "config.h"
#include "medyparam.h"   
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "esp_rom_sys.h"

// Pines I2C y dirección del módulo LCD
#define I2C_PORT   I2C_NUM_0
#define PIN_SDA    19
#define PIN_SCL    20
#define LCD_ADDR   0x27      

#define LCD_BL     0x08
#define LCD_EN     0x04
#define LCD_RS     0x01

static void i2c_send(uint8_t data)      // Envía un byte al módulo LCD a través de I2C
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data | LCD_BL, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
}

static void lcd_pulse(uint8_t data)     // Genera el pulso de habilitación para el LCD
{
    i2c_send(data | LCD_EN);
    esp_rom_delay_us(1);
    i2c_send(data & ~LCD_EN);
    esp_rom_delay_us(50);
}

static void lcd_write(uint8_t value, uint8_t rs)        // Envía un byte al LCD, dividiéndolo en dos nibbles (4 bits)
{
    lcd_pulse((value & 0xF0) | rs);
    lcd_pulse(((value << 4) & 0xF0) | rs);
}

static void lcd_cmd(uint8_t c)  { lcd_write(c, 0); }        
static void lcd_data(uint8_t d) { lcd_write(d, LCD_RS); }  

static void lcd_goto(uint8_t row, uint8_t col)
{
    lcd_cmd(0x80 | ((row == 0 ? 0x00 : 0x40) + col));
}

static void lcd_string(const char *s)
{
    while (*s) { lcd_data((uint8_t)*s++); }
}

static void lcd_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static void lcd_hw_init(void)       // Inicializa el hardware del LCD
{
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_pulse(0x30); esp_rom_delay_us(4500);
    lcd_pulse(0x30); esp_rom_delay_us(150);
    lcd_pulse(0x30); esp_rom_delay_us(150);
    lcd_pulse(0x20);
    lcd_cmd(0x28);   
    lcd_cmd(0x0C);   
    lcd_cmd(0x06);   
    lcd_cmd(0x01);   
    vTaskDelay(pdMS_TO_TICKS(20));
}

static const char *escala_txt(uint8_t e)        
{
    return (e == escala_500mv_div) ? "500mV" : "200mV";
}

static const char *base_txt(uint8_t b)
{
    if (b == base_0p4ms_div) return "0.4ms";
    if (b == base_1p0ms_div) return "1ms";
    return "0.2ms";
}

void lcd_init_task(void)
{
    xTaskCreate(task_lcd, "task_lcd", 4096, NULL, 2, NULL);
}

void task_lcd(void *param)
{
    (void)param;

    config_t cfg;
    char buf[24];

    lcd_i2c_init();
    lcd_hw_init();

    while (config == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (1)
    {
        if (xQueuePeek(config, &cfg, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            lcd_goto(0, 0);
            snprintf(buf, sizeof(buf), "V:%-5s %-4s",
                     escala_txt(cfg.escala_vertical),
                     (cfg.estado == med_staterun_config) ? "RUN" : "STOP");
            lcd_string(buf);

            lcd_goto(1, 0);
            snprintf(buf, sizeof(buf), "T%4.1f %c B:%-3s",
                     medyparam_trigger_mv_escalado(cfg.escala_vertical) / 1000.0f,  // en vivo, no el de config
                     (cfg.flanco == flanco_ascendente) ? '^' : 'v',
                     base_txt(cfg.base_tiempo));
            lcd_string(buf);
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}
