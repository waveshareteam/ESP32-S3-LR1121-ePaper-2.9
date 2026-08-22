idf_component_register(
    SRCS
        "app_main.c"
        "app_runtime.c"
        "app_ui.c"
        "app_buttons.c"
        "app_stress.c"
        "app_wifi.c"
        "app_ble.c"
        "app_radio_service.c"
        "test_system.c"
        "test_led.c"
        "test_i2c.c"
        "test_epaper.c"
        "test_battery.c"
        "test_rtc.c"
        "test_radio.c"
        "test_storage.c"
        "test_sleep.c"
    INCLUDE_DIRS "."
    REQUIRES esp_board_manager gen_bmgr_codes
             esp32_s3_lr1121_epaper_2_9_support esp_driver_gpio
             bt esp_driver_i2c esp_driver_sdmmc esp_driver_spi esp_event
             esp_hw_support esp_netif esp_partition esp_rom
             esp_system esp_timer esp_wifi fatfs freertos heap lwip mbedtls
             nvs_flash sdmmc spi_flash
)

