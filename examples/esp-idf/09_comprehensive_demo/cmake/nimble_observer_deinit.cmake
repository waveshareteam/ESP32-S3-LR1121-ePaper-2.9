# Some ESP-IDF NimBLE revisions initialize ble_hs_rx_q only when
# NIMBLE_BLE_CONNECT is enabled, but unconditionally deinitialize it. An
# observer-only host then dereferences an uninitialized event during teardown.
# Compile a corrected build-local copy; never modify the installed SDK.
function(fix_nimble_observer_deinit)
    if(NOT CONFIG_BT_NIMBLE_ENABLED)
        return()
    endif()

    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    idf_component_get_property(bt_dir bt COMPONENT_DIR)
    set(host_dir "${bt_dir}/host/nimble/nimble/nimble/host/src")
    set(original "${host_dir}/ble_hs.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" source)

    set(guarded "#if NIMBLE_BLE_CONNECT\n    ble_mqueue_deinit(&ble_hs_rx_q);\n#endif")
    string(FIND "${source}" "${guarded}" already_fixed)
    if(NOT already_fixed EQUAL -1)
        return()
    endif()

    set(old "    ble_mqueue_deinit(&ble_hs_rx_q);\n\n    ble_hs_stop_deinit();")
    string(FIND "${source}" "${old}" affected)
    if(affected EQUAL -1)
        message(FATAL_ERROR "NimBLE teardown changed; review the observer deinit compatibility fix")
    endif()
    string(REPLACE "${old}" "${guarded}\n\n    ble_hs_stop_deinit();" patched "${source}")

    get_target_property(bt_sources ${bt_lib} SOURCES)
    set(found FALSE)
    set(corrected_sources "")
    set(output "${CMAKE_BINARY_DIR}/nimble_compat/ble_hs.c")
    foreach(src IN LISTS bt_sources)
        get_filename_component(absolute "${src}" ABSOLUTE BASE_DIR "${bt_dir}")
        if(absolute STREQUAL original)
            list(APPEND corrected_sources "${output}")
            set(found TRUE)
        else()
            list(APPEND corrected_sources "${src}")
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "Cannot locate NimBLE ble_hs.c in bt target sources")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/nimble_compat")
    # Avoid recompiling this file when reconfiguration leaves the patch unchanged.
    file(WRITE "${output}.in" "${patched}")
    configure_file("${output}.in" "${output}" COPYONLY)
    set_property(TARGET ${bt_lib} PROPERTY SOURCES "${corrected_sources}")
    target_include_directories(${bt_lib} PRIVATE "${host_dir}")
    message(STATUS "NimBLE: guard observer-only RX queue deinit (build-local SDK fix)")
endfunction()
