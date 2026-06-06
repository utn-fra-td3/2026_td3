file(GLOB_RECURSE UI_SRCS "ui/*.c")

idf_component_register(
    SRCS "lcd_display.c" ${UI_SRCS}
    INCLUDE_DIRS "include" "ui"
    REQUIRES app_common driver esp_lcd espressif__esp_lvgl_port lvgl__lvgl
)
