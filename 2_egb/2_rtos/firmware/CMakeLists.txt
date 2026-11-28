idf_component_register(SRCS "main.c" 
                            "oled_driver.c" 
                            "mux_driver.c" 
                            "encoder_driver.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash driver esp_timer)

                       