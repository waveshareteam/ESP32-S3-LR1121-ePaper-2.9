set(example_sources "main.c")
if(CONFIG_EPAPER_DEMO_PHOTO_HF OR CONFIG_EPAPER_DEMO_PHOTO_LF)
    list(APPEND example_sources "photo_display.c")
endif()

idf_component_register(SRCS ${example_sources}
                       INCLUDE_DIRS "."
                       REQUIRES esp32_s3_lr1121_epaper_2_9_support esp_board_manager
                                gen_bmgr_codes freertos log)
