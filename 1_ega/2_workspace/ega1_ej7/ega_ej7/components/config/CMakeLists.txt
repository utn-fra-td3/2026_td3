idf_component_register(
    SRCS "config.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos uart_comm flash log
)