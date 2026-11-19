idf_component_register(
    SRCS "flash.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos nvs_flash config
)