idf_component_register(
    SRCS "main.cpp"
         "abp_node.cpp"
         "abp_state.cpp"
         "abp_ui.cpp"
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
