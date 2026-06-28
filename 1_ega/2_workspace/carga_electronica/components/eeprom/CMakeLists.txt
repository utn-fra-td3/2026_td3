idf_component_register(SRCS "eeprom.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver system_manager esp_driver_i2c)
