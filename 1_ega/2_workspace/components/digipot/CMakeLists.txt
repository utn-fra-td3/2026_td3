idf_component_register(
    SRCS "digipot.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_spi esp_driver_gpio config
)