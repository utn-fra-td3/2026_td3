idf_component_register(
    SRCS "lcd_display.c"
    INCLUDE_DIRS "include"
    REQUIRES app_common driver esp_lcd espressif__esp_lvgl_port lvgl__lvgl
)
