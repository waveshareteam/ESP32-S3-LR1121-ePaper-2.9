idf_component_register(
    SRCS "src/board_support.c"
         "src/board_epaper.c"
         "src/board_lorawan_radio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES epaper_ui_font esp_board_manager gen_bmgr_codes
             esp_driver_gpio esp_driver_spi esp_timer freertos
             esp_epaper lvgl__lvgl jgromes__radiolib
)
