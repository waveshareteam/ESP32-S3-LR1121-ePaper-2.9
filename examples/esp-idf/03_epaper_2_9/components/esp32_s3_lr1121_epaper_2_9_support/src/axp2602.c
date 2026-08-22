#include "axp2602.h"

#include <stddef.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2602_I2C_SPEED_HZ       (400000)
#define AXP2602_I2C_TIMEOUT_MS     (100)
#define AXP2602_MCU_RESET_PULSE_MS (50)
#define AXP2602_MCU_RESTART_MS     (100)
#define AXP2602_SOC_READY_DELAY_MS (350)

#define AXP2602_REG_ID              (0x00)
#define AXP2602_REG_MODE            (0x02)
#define AXP2602_REG_VBAT_HIGH       (0x04)
#define AXP2602_REG_IC_TEMPERATURE  (0x06)
#define AXP2602_REG_SOC             (0x08)
#define AXP2602_REG_TTE_HIGH        (0x0A)
#define AXP2602_REG_TTF_HIGH        (0x0C)
#define AXP2602_REG_COMM_CONFIG     (0x11)
#define AXP2602_REG_IBAT_HIGH       (0x14)
#define AXP2602_REG_OPERATING_MODE  (0xC6)

#define AXP2602_MODE_RESET_MCU      (1U << 5)
#define AXP2602_MODE_SLEEP          (1U << 0)
#define AXP2602_MEASURE_TDIE        (1U << 6)
#define AXP2602_MEASURE_CURRENT     (1U << 5)
#define AXP2602_MEASURE_VBAT        (1U << 4)
#define AXP2602_SENSE_MASK          (0x03)
#define AXP2602_REQUIRED_COMM_MASK  (AXP2602_MEASURE_TDIE | AXP2602_MEASURE_CURRENT | \
                                     AXP2602_MEASURE_VBAT | AXP2602_SENSE_MASK)
#define AXP2602_ID_MASK             (0x3F)
#define AXP2602_OPERATING_MODE_MASK (0x07)
#define AXP2602_OPERATING_NORMAL    (0x02)
#define AXP2602_VBAT_MAX_MV         (4500)

static esp_err_t axp2602_read_reg(axp2602_t *device, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(device->i2c_device, &reg, 1, value, 1,
                                       AXP2602_I2C_TIMEOUT_MS);
}

static esp_err_t axp2602_read_regs(axp2602_t *device, uint8_t first_reg,
                                   uint8_t *values, size_t value_count)
{
    return i2c_master_transmit_receive(device->i2c_device, &first_reg, 1,
                                       values, value_count, AXP2602_I2C_TIMEOUT_MS);
}

static esp_err_t axp2602_write_reg(axp2602_t *device, uint8_t reg, uint8_t value)
{
    const uint8_t buffer[] = {reg, value};
    return i2c_master_transmit(device->i2c_device, buffer, sizeof(buffer),
                               AXP2602_I2C_TIMEOUT_MS);
}

static esp_err_t axp2602_read_be16(axp2602_t *device, uint8_t high_reg, uint16_t *value)
{
    uint8_t bytes[2] = {0};
    esp_err_t ret = axp2602_read_regs(device, high_reg, bytes, sizeof(bytes));
    if (ret != ESP_OK) {
        return ret;
    }
    *value = ((uint16_t)bytes[0] << 8) | bytes[1];
    return ESP_OK;
}

static float axp2602_current_resolution_ma(axp2602_sense_resistor_t sense_resistor)
{
    switch (sense_resistor) {
    case AXP2602_SENSE_RESISTOR_5_MOHM:
        return 0.5f;
    case AXP2602_SENSE_RESISTOR_20_MOHM:
        return 0.125f;
    case AXP2602_SENSE_RESISTOR_40_MOHM:
        return 0.0625f;
    case AXP2602_SENSE_RESISTOR_10_MOHM:
    default:
        return 0.25f;
    }
}

esp_err_t axp2602_init(axp2602_t *device,
                       i2c_master_bus_handle_t bus,
                       axp2602_sense_resistor_t sense_resistor)
{
    if (device == NULL || bus == NULL ||
        (unsigned)sense_resistor > (unsigned)AXP2602_SENSE_RESISTOR_40_MOHM) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device->i2c_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2602_I2C_ADDRESS,
        .scl_speed_hz = AXP2602_I2C_SPEED_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &config, &device->i2c_device);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t value = 0;
    ret = axp2602_read_reg(device, AXP2602_REG_ID, &value);
    if (ret != ESP_OK) {
        goto fail;
    }
    if ((value & AXP2602_ID_MASK) != AXP2602_CHIP_ID) {
        ret = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    ret = axp2602_read_reg(device, AXP2602_REG_MODE, &value);
    if (ret != ESP_OK) {
        goto fail;
    }
    const uint8_t mode_awake =
        value & (uint8_t)~(AXP2602_MODE_RESET_MCU | AXP2602_MODE_SLEEP);
    ret = axp2602_write_reg(device, AXP2602_REG_MODE, mode_awake);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = axp2602_read_reg(device, AXP2602_REG_COMM_CONFIG, &value);
    if (ret != ESP_OK) {
        goto fail;
    }
    const uint8_t requested_comm_config =
        (value & (uint8_t)~AXP2602_SENSE_MASK) |
        AXP2602_MEASURE_TDIE |
        AXP2602_MEASURE_CURRENT |
        AXP2602_MEASURE_VBAT |
        (uint8_t)sense_resistor;
    const bool sense_resistor_changed =
        (value & AXP2602_SENSE_MASK) != (uint8_t)sense_resistor;
    if (value != requested_comm_config) {
        ret = axp2602_write_reg(device, AXP2602_REG_COMM_CONFIG,
                                requested_comm_config);
        if (ret != ESP_OK) {
            goto fail;
        }
    }

    if (sense_resistor_changed) {
        ret = axp2602_write_reg(device, AXP2602_REG_MODE,
                                mode_awake | AXP2602_MODE_RESET_MCU);
        if (ret != ESP_OK) {
            goto fail;
        }
        vTaskDelay(pdMS_TO_TICKS(AXP2602_MCU_RESET_PULSE_MS));

        ret = axp2602_write_reg(device, AXP2602_REG_MODE, mode_awake);
        if (ret != ESP_OK) {
            goto fail;
        }
        vTaskDelay(pdMS_TO_TICKS(AXP2602_MCU_RESTART_MS));

        ret = axp2602_read_reg(device, AXP2602_REG_MODE, &value);
        if (ret != ESP_OK) {
            goto fail;
        }
        value &= (uint8_t)~(AXP2602_MODE_RESET_MCU | AXP2602_MODE_SLEEP);
        ret = axp2602_write_reg(device, AXP2602_REG_MODE, value);
        if (ret != ESP_OK) {
            goto fail;
        }
    }

    ret = axp2602_read_reg(device, AXP2602_REG_COMM_CONFIG, &value);
    if (ret != ESP_OK) {
        goto fail;
    }
    if ((value & AXP2602_REQUIRED_COMM_MASK) !=
        (requested_comm_config & AXP2602_REQUIRED_COMM_MASK)) {
        ret = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }

    ret = axp2602_read_reg(device, AXP2602_REG_OPERATING_MODE, &value);
    if (ret != ESP_OK) {
        goto fail;
    }
    value = (value & (uint8_t)~AXP2602_OPERATING_MODE_MASK) |
            AXP2602_OPERATING_NORMAL;
    ret = axp2602_write_reg(device, AXP2602_REG_OPERATING_MODE, value);
    if (ret != ESP_OK) {
        goto fail;
    }

    device->sense_resistor = sense_resistor;
    vTaskDelay(pdMS_TO_TICKS(AXP2602_SOC_READY_DELAY_MS));
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(device->i2c_device);
    device->i2c_device = NULL;
    return ret;
}

esp_err_t axp2602_read(axp2602_t *device, axp2602_data_t *data)
{
    if (device == NULL || device->i2c_device == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t voltage_raw = 0;
    uint16_t current_raw = 0;
    uint16_t time_to_empty = 0;
    uint16_t time_to_full = 0;
    uint8_t temperature_raw = 0;
    uint8_t soc = 0;

    esp_err_t ret = axp2602_read_be16(device, AXP2602_REG_VBAT_HIGH, &voltage_raw);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = axp2602_read_be16(device, AXP2602_REG_IBAT_HIGH, &current_raw);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = axp2602_read_reg(device, AXP2602_REG_IC_TEMPERATURE, &temperature_raw);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = axp2602_read_reg(device, AXP2602_REG_SOC, &soc);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = axp2602_read_be16(device, AXP2602_REG_TTE_HIGH, &time_to_empty);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = axp2602_read_be16(device, AXP2602_REG_TTF_HIGH, &time_to_full);
    if (ret != ESP_OK) {
        return ret;
    }

    const uint16_t voltage_mv = voltage_raw & 0x3FFFU;
    if (voltage_mv > AXP2602_VBAT_MAX_MV || soc > 100) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const int32_t signed_current = (current_raw & 0x8000U)
                                       ? (int32_t)current_raw - 0x10000
                                       : (int32_t)current_raw;
    const int16_t current = (int16_t)signed_current;
    const int32_t signed_temperature = (temperature_raw & 0x80U)
                                           ? (int32_t)temperature_raw - 0x100
                                           : (int32_t)temperature_raw;

    const axp2602_data_t result = {
        .voltage_mv = voltage_mv,
        .current_raw = current,
        .current_ma = current * axp2602_current_resolution_ma(device->sense_resistor),
        .ic_temperature_c = (int8_t)signed_temperature,
        .soc_percent = soc,
        .time_to_empty_min = time_to_empty,
        .time_to_full_min = time_to_full,
    };
    *data = result;
    return ESP_OK;
}

esp_err_t axp2602_sleep(axp2602_t *device)
{
    if (device == NULL || device->i2c_device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mode = 0;
    esp_err_t ret = axp2602_read_reg(device, AXP2602_REG_MODE, &mode);
    if (ret != ESP_OK) {
        return ret;
    }
    return axp2602_write_reg(device, AXP2602_REG_MODE, mode | AXP2602_MODE_SLEEP);
}

esp_err_t axp2602_wake(axp2602_t *device)
{
    if (device == NULL || device->i2c_device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mode = 0;
    esp_err_t ret = axp2602_read_reg(device, AXP2602_REG_MODE, &mode);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = axp2602_write_reg(device, AXP2602_REG_MODE,
                            mode & (uint8_t)~(AXP2602_MODE_RESET_MCU |
                                              AXP2602_MODE_SLEEP));
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(AXP2602_SOC_READY_DELAY_MS));
    }
    return ret;
}

esp_err_t axp2602_deinit(axp2602_t *device)
{
    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device->i2c_device == NULL) {
        return ESP_OK;
    }
    esp_err_t ret = i2c_master_bus_rm_device(device->i2c_device);
    if (ret == ESP_OK) {
        device->i2c_device = NULL;
    }
    return ret;
}
