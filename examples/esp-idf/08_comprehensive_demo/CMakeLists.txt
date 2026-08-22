cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults"
    "components/gen_bmgr_codes/board_manager.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(08_comprehensive_demo)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/esp_idf_6_2_workarounds.cmake)

