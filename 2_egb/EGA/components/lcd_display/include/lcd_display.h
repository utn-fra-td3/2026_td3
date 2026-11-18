#pragma once

#include "esp_err.h"

esp_err_t lcd_display_init(void);
esp_err_t lcd_display_write_screen(const char *line1, const char *line2);
