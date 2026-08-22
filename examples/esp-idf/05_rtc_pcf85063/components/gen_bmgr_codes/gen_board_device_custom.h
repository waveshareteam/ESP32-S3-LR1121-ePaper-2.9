/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated custom device structure definitions
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Custom device structure definitions
// These structures are dynamically generated based on YAML configuration

// Structure definition for epaper_ssd16xx
typedef struct {
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
    int8_t       busy_gpio;
    int8_t       reset_gpio;
    int8_t       dc_gpio;
    int8_t       cs_gpio;
    int32_t      spi_speed_hz;
    uint8_t     peripheral_count;
    const char *peripheral_name;
} dev_custom_epaper_ssd16xx_config_t;

// Structure definition for rtc_pcf85063a
typedef struct {
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
    int8_t       address;
    int32_t      frequency;
    int8_t       int_gpio;
    uint8_t     peripheral_count;
    const char *peripheral_name;
} dev_custom_rtc_pcf85063a_config_t;

// Structure definition for bat_axp2602
typedef struct {
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
    int8_t       address;
    int32_t      frequency;
    int8_t       sense_resistor_mohm;
    int8_t       idle_current_threshold_ma;
    uint8_t     peripheral_count;
    const char *peripheral_name;
} dev_custom_bat_axp2602_config_t;

// Structure definition for radio_lr1121
typedef struct {
    const char *name;           /*!< Custom device name */
    const char *type;           /*!< Device type: "custom" */
    const char *chip;           /*!< Chip name */
    int8_t       dio1_gpio;
    int8_t       nss_gpio;
    int8_t       reset_gpio;
    int8_t       busy_gpio;
    int8_t       dio7_gpio;
    int8_t       dio8_gpio;
    int32_t      spi_speed_hz;
    uint8_t     peripheral_count;
    const char *peripheral_name;
} dev_custom_radio_lr1121_config_t;

