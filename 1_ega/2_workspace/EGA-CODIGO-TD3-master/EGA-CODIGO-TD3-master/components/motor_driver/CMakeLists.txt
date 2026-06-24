idf_component_register(
    SRCS "motor_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES app_config esp_driver_gpio esp_driver_ledc
)
