idf_component_register(SRCS "trigger.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos esp_driver_gpio config)
