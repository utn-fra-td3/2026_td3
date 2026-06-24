idf_component_register(
    SRCS "lcd_display.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config driver esp-idf-lib__hd44780
)
