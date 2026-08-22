#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2602_I2C_ADDRESS (0x62)
#define AXP2602_CHIP_ID     (0x1C)

typedef enum {
    AXP2602_SENSE_RESISTOR_10_MOHM = 0,
    AXP2602_SENSE_RESISTOR_5_MOHM = 1,
    AXP2602_SENSE_RESISTOR_20_MOHM = 2,
    AXP2602_SENSE_RESISTOR_40_MOHM = 3,
} axp2602_sense_resistor_t;

typedef struct {
    i2c_master_dev_handle_t i2c_device;
    axp2602_sense_resistor_t sense_resistor;
} axp2602_t;

typedef struct {
    uint16_t voltage_mv;
    int16_t current_raw;
    float current_ma;
    int8_t ic_temperature_c;
    uint8_t soc_percent;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
} axp2602_data_t;

esp_err_t axp2602_init(axp2602_t *device,
                       i2c_master_bus_handle_t bus,
                       axp2602_sense_resistor_t sense_resistor);
esp_err_t axp2602_read(axp2602_t *device, axp2602_data_t *data);
esp_err_t axp2602_wake(axp2602_t *device);
esp_err_t axp2602_sleep(axp2602_t *device);
esp_err_t axp2602_deinit(axp2602_t *device);

#ifdef __cplusplus
}
#endif
