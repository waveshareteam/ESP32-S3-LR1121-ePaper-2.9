idf_component_register(
    SRCS "main.cpp"
         "lorawan_node.cpp"
         "lorawan_payload.cpp"
         "lorawan_runtime.cpp"
         "lorawan_session.cpp"
         "lorawan_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_board_manager
             gen_bmgr_codes
             esp32_s3_lr1121_epaper_2_9_support
             esp_hw_support
             esp_system
             esp_timer
             freertos
             jgromes__radiolib
             nvs_flash
)
