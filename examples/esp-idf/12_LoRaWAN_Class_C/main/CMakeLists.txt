idf_component_register(
    SRCS "main.cpp"
         "lorawan_app.cpp"
         "lorawan_activation.cpp"
         "lorawan_node.cpp"
         "lorawan_persistence.cpp"
         "lorawan_runtime.cpp"
         "lorawan_state.cpp"
         "lorawan_session.cpp"
         "lorawan_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_board_manager
             gen_bmgr_codes
             esp32_s3_lr1121_epaper_2_9_support
             esp_timer
             freertos
             jgromes__radiolib
             nvs_flash
)
