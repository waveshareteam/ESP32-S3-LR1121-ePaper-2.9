idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES fatfs sdmmc esp_common esp_driver_gpio
                                esp_driver_sdmmc freertos log)