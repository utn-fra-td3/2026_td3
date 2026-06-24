idf_component_register(
    SRCS "uart_comm.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_driver_uart config
)