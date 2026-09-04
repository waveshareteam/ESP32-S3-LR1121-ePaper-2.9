idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_board_manager gen_bmgr_codes led_strip
                                freertos log)
