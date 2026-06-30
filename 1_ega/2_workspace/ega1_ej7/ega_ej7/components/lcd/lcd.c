#include <stdint.h>
#include "lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>
#include "config.h"

#define LCD_I2C_SDA        GPIO_NUM_21
#define LCD_I2C_SCL        GPIO_NUM_20
#define LCD_I2C_PORT       I2C_NUM_0
#define LCD_I2C_FREQ_HZ    100000

#define LCD_I2C_ADDR       0x3F

#define LCD_BACKLIGHT      0x08 // Backlight control bit
#define LCD_ENABLE_BIT     0x04 // Enable bit

#define LCD_COMMAND        0x00
#define LCD_CHARACTER      0x01

#define LCD_CLEARDISPLAY   0x01
#define LCD_ENTRYMODESET   0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_FUNCTIONSET    0x20
#define LCD_SETDDRAMADDR   0x80

#define LCD_ENTRYLEFT      0x02
#define LCD_DISPLAYON      0x04
#define LCD_2LINE          0x08

static i2c_master_bus_handle_t i2c_bus_handle;
static i2c_master_dev_handle_t lcd_dev_handle;


static void lcd_i2c_write(uint8_t data) // Escribe un byte al LCD a través de I2C
{
    i2c_master_transmit(
        lcd_dev_handle,
        &data,
        1,
        pdMS_TO_TICKS(100)
    );
}


static void lcd_pulse_enable(uint8_t data) // Genera un pulso de habilitación para el LCD
{
    lcd_i2c_write(data | LCD_ENABLE_BIT);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_i2c_write(data & ~LCD_ENABLE_BIT);
    vTaskDelay(pdMS_TO_TICKS(5));
}


static void lcd_write_nibble(uint8_t nibble, uint8_t mode) // Escribe un nibble (4 bits) al LCD en modo 4 bits
{ 
    uint8_t data = (nibble << 4) | LCD_BACKLIGHT | mode;

    lcd_i2c_write(data);
    lcd_pulse_enable(data);
}


static void lcd_send_byte(uint8_t value, uint8_t mode) // Envía un byte al LCD, dividiéndolo en dos nibbles y enviándolos en modo 4 bits
{
    uint8_t high = (value >> 4) & 0x0F;
    uint8_t low  = value & 0x0F;

    lcd_write_nibble(high, mode);
    lcd_write_nibble(low, mode);

    vTaskDelay(pdMS_TO_TICKS(5));
}

static void lcd_clear(void) 
{
    lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t row_offsets[] = {0x00, 0x40};

    lcd_send_byte(
        LCD_SETDDRAMADDR | (row_offsets[row] + col),
        LCD_COMMAND
    );

    vTaskDelay(pdMS_TO_TICKS(10));
}

static void lcd_print_16(const char *str) // Imprime una cadena de hasta 16 caracteres en el LCD, rellenando con espacios si es más corta
{
    uint8_t i = 0;

    while (str[i] != '\0' && i < 16)
    {
        lcd_send_byte((uint8_t)str[i], LCD_CHARACTER);
        i++;
    }

    while (i < 16)
    {
        lcd_send_byte(' ', LCD_CHARACTER);
        i++;
    }
}


static void lcd_hw_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));


    // Secuencia de inicialización en nibbles.

    lcd_write_nibble(0x03, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_write_nibble(0x03, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_write_nibble(0x03, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(10));

    lcd_write_nibble(0x02, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configuración final del LCD.

    lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    vTaskDelay(pdMS_TO_TICKS(5));

    lcd_clear();
}


static void i2c_lcd_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = LCD_I2C_PORT,
        .sda_io_num = LCD_I2C_SDA,
        .scl_io_num = LCD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    i2c_new_master_bus(&bus_config, &i2c_bus_handle);

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_I2C_ADDR,
        .scl_speed_hz = LCD_I2C_FREQ_HZ
    };

    i2c_master_bus_add_device(
        i2c_bus_handle,
        &dev_config,
        &lcd_dev_handle
    );

    lcd_hw_init();
}

void lcd_init_task(void)
{
    xTaskCreate(task_lcd, "task_lcd", 4096, NULL, 2, NULL);
}

static const char *estado_to_text(uint8_t estado) 
{
    if (estado == med_staterun_config)
    {
        return "RUN";
    }

    return "OFF";
}

static const char *escala_vertical_to_text(uint8_t escala)
{
    if (escala == escala_1v_div)
    {
        return "1";
    }

    return "5";
}

static const char *base_tiempo_to_text(uint8_t base)
{
    if (base == base_1ms_div)
    {
        return "1ms";
    }
    else if (base == base_2ms_div)
    {
        return "2ms";
    }

    return "5ms";
}

static char flanco_to_char(uint8_t flanco)
{
    if (flanco == flanco_ascendente)
    {
        return 'A';
    }

    return 'D';
}

void task_lcd(void *param)
{
    (void)param;

    config_t config_leida;
    config_t config_anterior;

    uint8_t primera_vez = 1;

    char linea1[17]; 
    char linea2[17];

    char linea1_anterior[17] = {0};
    char linea2_anterior[17] = {0};

    while (config == NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    i2c_lcd_init();

    lcd_clear();

    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1)
    {
        if (xQueuePeek(config, &config_leida, pdMS_TO_TICKS(500)) == pdTRUE)
        {
            if (primera_vez ||
                memcmp(&config_leida, &config_anterior, sizeof(config_t)) != 0) // Si es la primera vez o si hay cambios en la configuración
            {

                vTaskDelay(pdMS_TO_TICKS(300));

                xQueuePeek(config, &config_leida, pdMS_TO_TICKS(100));

                snprintf(
                    linea1,
                    sizeof(linea1),
                    "%s V:%s H:%s",
                    estado_to_text(config_leida.estado),
                    escala_vertical_to_text(config_leida.escala_vertical),
                    base_tiempo_to_text(config_leida.base_tiempo)
                );

                snprintf(
                    linea2,
                    sizeof(linea2),
                    "Tg:%d F:%c",
                    config_leida.nivel_trigger,
                    flanco_to_char(config_leida.flanco)
                );


                if (primera_vez || strcmp(linea1, linea1_anterior) != 0) // Si es la primera vez o si hay cambios en la línea 1
                {
                    lcd_set_cursor(0, 0);
                    lcd_print_16(linea1);

                    strncpy(linea1_anterior, linea1, sizeof(linea1_anterior));
                    linea1_anterior[16] = '\0';

                    vTaskDelay(pdMS_TO_TICKS(200));
                }

                if (primera_vez || strcmp(linea2, linea2_anterior) != 0)
                {
                    lcd_set_cursor(1, 0);
                    lcd_print_16(linea2);

                    strncpy(linea2_anterior, linea2, sizeof(linea2_anterior));
                    linea2_anterior[16] = '\0';

                    vTaskDelay(pdMS_TO_TICKS(200));
                }

                config_anterior = config_leida;
                primera_vez = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}