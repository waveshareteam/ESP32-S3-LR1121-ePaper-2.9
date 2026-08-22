idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_s3_lr1121_epaper_2_9_support esp_board_manager
                                esp_driver_gpio gen_bmgr_codes freertos log)
