# Board information output
message(STATUS "Selected Board: esp32_s3_lr1121_epaper_2_9")
message(STATUS "Board Path: /home/hsw/esp/esp32-s3-lr1121-ePaper-2.9/demo/components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9")

idf_component_register(
    SRC_DIRS "." "../../../components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9"
    INCLUDE_DIRS "." "../../../components/waveshare_esp_boards/esp32_s3_lr1121_epaper_2_9"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
