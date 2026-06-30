idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES config uart_comm esp_driver_gpio flash lcd teclado
)