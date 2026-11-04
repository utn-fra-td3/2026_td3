idf_component_register(
    SRCS "medyparam.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos config trigger adc
)