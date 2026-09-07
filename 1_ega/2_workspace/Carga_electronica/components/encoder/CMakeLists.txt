idf_component_register(SRCS "encoder.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_driver_pcnt esp_driver_gpio pin_setting esp_timer lvgl__lvgl)
