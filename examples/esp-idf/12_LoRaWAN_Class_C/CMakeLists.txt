cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "sdkconfig.defaults"
    "components/gen_bmgr_codes/board_manager.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(12_LoRaWAN_Class_C)
