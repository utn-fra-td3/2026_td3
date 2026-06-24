idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_config
        as5600_sensor
        angle_utils
        lcd_display
        motor_driver
        driver
        esp_driver_gpio
        esp_driver_uart
        nvs_flash
)
