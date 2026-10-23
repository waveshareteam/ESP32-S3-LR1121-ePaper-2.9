idf_component_register(
    SRCS "src/board_support.c"
         "src/board_epaper.c"
         "src/board_radio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES epaper_ui_font esp_board_manager gen_bmgr_codes
             esp_driver_gpio esp_driver_i2c esp_driver_spi esp_timer freertos led_strip
             esp_epaper lvgl__lvgl jgromes__radiolib
             waveshare__pcf85063a
)
