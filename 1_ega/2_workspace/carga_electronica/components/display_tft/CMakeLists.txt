idf_component_register(SRCS "display_tft.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_lcd esp_lcd_st7789 esp_lvgl_port lvgl system_manager adc_sensor)
