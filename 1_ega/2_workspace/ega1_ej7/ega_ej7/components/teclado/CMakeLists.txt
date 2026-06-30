idf_component_register(
    SRCS "teclado.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_gpio config
)