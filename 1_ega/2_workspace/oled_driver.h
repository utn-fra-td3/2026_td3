#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Inicializa el bus I2C y configura la pantalla OLED SSD1306.
 * @return esp_err_t ESP_OK si la inicialización es exitosa.
 */
esp_err_t oled_driver_init(void);

/**
 * @brief Escribe un texto en una celda de la grilla definida.
 * @param grid_col Columna (0 o 1).
 * @param grid_row Fila (0, 1 o 2).
 * @param text Cadena de caracteres a mostrar.
 */
void oled_write_grid(uint8_t grid_col, uint8_t grid_row, const char *text);

/**
 * @brief Limpia toda la pantalla OLED.
 */
void ssd1306_clear(void);

/**
 * @brief Limpia específicamente el área de una celda de la grilla.
 * @param grid_col Columna (0 o 1).
 * @param grid_row Fila (0, 1 o 2).
 */
void ssd1306_clear_cell(uint8_t grid_col, uint8_t grid_row);

#endif // OLED_DRIVER_H