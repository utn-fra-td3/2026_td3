file(GLOB_RECURSE UI_SRCS "ui/*.c")

idf_component_register(
    SRCS "lcd_display.c" "ui_scrconfig_kb.c" ${UI_SRCS}
    INCLUDE_DIRS "include" "ui"
    REQUIRES app_common driver esp_lcd espressif__esp_lvgl_port lvgl__lvgl
             espressif__esp_lcd_touch espressif__esp_lcd_touch_cst816s
)
