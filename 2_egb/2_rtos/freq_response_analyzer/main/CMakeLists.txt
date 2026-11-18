idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES app_common user_controls menu_config lcd_display sweep uart nvs_store)