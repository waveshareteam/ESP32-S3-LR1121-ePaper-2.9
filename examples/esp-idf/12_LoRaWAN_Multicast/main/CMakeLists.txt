idf_component_register(
    SRCS "main.cpp"
         "lorawan_profile.cpp"
         "radio_runtime.cpp"
         "multicast_session.cpp"
         "lorawan_flow.cpp"
         "multicast_store.cpp"
         "multicast_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_board_manager
             gen_bmgr_codes
             esp32_s3_lr1121_epaper_2_9_support
             esp_timer
             freertos
             jgromes__radiolib
             nvs_flash
)
