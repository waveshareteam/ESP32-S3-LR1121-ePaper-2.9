idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_board_manager gen_bmgr_codes
                                esp32_s3_lr1121_epaper_2_9_support esp_driver_gpio
                                esp_hw_support freertos)
