idf_component_register(
    SRCS "lcd.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_i2c esp_driver_gpio config 
)