# SensorLib 0.4.1 prints a float with %d and turns invalid TDIE samples into 0 C.
# Use a build-local header overlay so dependency downloads remain untouched.
function(fix_sensorlib_temperature)
    idf_component_get_property(sensorlib_dir lewisxhe__sensorlib COMPONENT_DIR)
    idf_component_get_property(main_lib main COMPONENT_LIB)
    set(original "${sensorlib_dir}/src/GaugeAXP2602.hpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" source)

    set(old [=[        // Read die temperature (13-bit ADC)
        int tdieHigh = readReg(REG_TDIE_ADC_HIGH);
        int tdieLow = readReg(REG_TDIE_ADC_LOW);
        if (tdieHigh >= 0 && tdieLow >= 0) {
            data.tdieTemperatureRaw = ((tdieHigh & 0x1F) << 8) | tdieLow;  // High 5 bits + Low 8 bits
            // Formula on page 10 of the manual: Temperature (°C) = (3415 - xDie) * 10 / 95 + 26
            data.actualDieTemperature = (3415.0 - data.tdieTemperatureRaw) * 10.0 / 95.0 + 26.0;
            if (data.actualDieTemperature < -40 || data.actualDieTemperature > 85) {
                log_e("Die temperature out of range: %d\n", data.actualDieTemperature);
                data.actualDieTemperature = 0; // Reset to 0 if out of range
            }
        }
]=])
    set(corrected [=[        // Read die temperature (13-bit ADC). Never retain a stale or fake 0 C sample.
        data.actualDieTemperature = NAN;
        data.tdieTemperatureRaw = 0;
        int tdieHigh = readReg(REG_TDIE_ADC_HIGH);
        int tdieLow = readReg(REG_TDIE_ADC_LOW);
        if (tdieHigh >= 0 && tdieLow >= 0) {
            data.tdieTemperatureRaw = ((tdieHigh & 0x1F) << 8) | tdieLow;
            const float temperature = (3415.0f - data.tdieTemperatureRaw) * 10.0f / 95.0f + 26.0f;
            if (temperature < -40.0f || temperature > 85.0f) {
                log_w("Die temperature invalid: raw=%u calculated=%.1fC\n",
                      (unsigned)data.tdieTemperatureRaw, (double)temperature);
            } else {
                data.actualDieTemperature = temperature;
            }
        }
]=])
    string(FIND "${source}" "${old}" affected)
    if(affected EQUAL -1)
        message(FATAL_ERROR "SensorLib TDIE implementation changed; review the temperature compatibility fix")
    endif()
    string(REPLACE "${old}" "${corrected}" patched "${source}")
    set(output "${CMAKE_BINARY_DIR}/sensorlib_compat/GaugeAXP2602.hpp")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/sensorlib_compat")
    file(WRITE "${output}.in" "#include <math.h>\n${patched}")
    configure_file("${output}.in" "${output}" COPYONLY)
    target_include_directories(${main_lib} BEFORE PRIVATE "${CMAKE_BINARY_DIR}/sensorlib_compat")
    message(STATUS "SensorLib: validate TDIE samples and correct float logging (build-local header)")
endfunction()
