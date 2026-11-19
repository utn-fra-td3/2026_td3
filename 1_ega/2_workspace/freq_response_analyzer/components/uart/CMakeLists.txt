idf_component_register(
    SRCS "uart.c"
    INCLUDE_DIRS "include"
    REQUIRES app_common esp_driver_uart
)
