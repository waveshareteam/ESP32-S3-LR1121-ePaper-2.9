#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2602_I2C_ADDRESS (0x62)
#define AXP2602_CHIP_ID     (0x1C)

#define AXP2602_IRQ_LOW_SOC             (1U << 0)
#define AXP2602_IRQ_SOC_UPDATE          (1U << 1)
#define AXP2602_IRQ_OVER_TEMPERATURE    (1U << 2)
#define AXP2602_IRQ_WATCHDOG_OVERFLOW   (1U << 3)
#define AXP2602_IRQ_ALL                 (AXP2602_IRQ_LOW_SOC | \
                                         AXP2602_IRQ_SOC_UPDATE | \
                                         AXP2602_IRQ_OVER_TEMPERATURE | \
                                         AXP2602_IRQ_WATCHDOG_OVERFLOW)

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
esp_err_t axp2602_configure_irq(axp2602_t *device,
                                uint8_t enabled_events,
                                bool pulse_mode);
esp_err_t axp2602_get_irq_status(axp2602_t *device, uint8_t *status);
esp_err_t axp2602_clear_irq_status(axp2602_t *device, uint8_t status);
esp_err_t axp2602_wake(axp2602_t *device);
esp_err_t axp2602_sleep(axp2602_t *device);
esp_err_t axp2602_deinit(axp2602_t *device);

#ifdef __cplusplus
}
#endif
