idf_component_register(
    SRCS "lcd.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos driver esp_driver_gpio config medyparam
)
