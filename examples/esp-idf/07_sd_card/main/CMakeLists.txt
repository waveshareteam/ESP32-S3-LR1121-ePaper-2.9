idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_s3_lr1121_epaper_2_9_support
                                esp_board_manager gen_bmgr_codes fatfs sdmmc esp_common esp_driver_gpio
                                esp_driver_sdmmc freertos log)
