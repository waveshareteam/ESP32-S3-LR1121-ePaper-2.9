cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
# Allow dependency downloads before board code has been generated.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/components/gen_bmgr_codes/board_manager.defaults")
    list(APPEND SDKCONFIG_DEFAULTS "components/gen_bmgr_codes/board_manager.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(09_comprehensive_demo)

include(cmake/nimble_observer_deinit.cmake)
fix_nimble_observer_deinit()
