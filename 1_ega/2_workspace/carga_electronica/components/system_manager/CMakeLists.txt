idf_component_register(SRCS "system_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES encoder adc_sensor display_tft eeprom)
