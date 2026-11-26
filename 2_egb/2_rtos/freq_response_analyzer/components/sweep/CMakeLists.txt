idf_component_register(
    SRCS "sweep.c" "ad9833.c" "adc.c"
    INCLUDE_DIRS "include"
    REQUIRES app_common esp_adc esp_driver_spi esp_driver_gpio
)
