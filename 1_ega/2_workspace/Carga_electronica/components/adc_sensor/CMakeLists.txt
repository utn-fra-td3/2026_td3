idf_component_register(SRCS "adc_sensor.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_adc pin_setting system_manager display_tft)
