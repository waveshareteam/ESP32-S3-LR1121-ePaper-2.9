idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_s3_lr1121_epaper_2_9_support esp_board_manager sensorlib
                                esp_driver_gpio esp_driver_i2c gen_bmgr_codes freertos log)
