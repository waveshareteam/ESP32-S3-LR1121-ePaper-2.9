/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device handle definition file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stddef.h>
#include "esp_board_device.h"
#include "dev_custom.h"
#include "dev_led_strip.h"
#include "dev_power_ctrl.h"

// Device handle array
esp_board_device_handle_t g_esp_board_device_handles[] = {
    {
        .next = &g_esp_board_device_handles[1],
        .name = "status_led",
        .chip = "ws2812",
        .type = "led_strip",
        .device_handle = NULL,
        .init = dev_led_strip_init,
        .deinit = dev_led_strip_deinit
    },
    {
        .next = &g_esp_board_device_handles[2],
        .name = "epaper_power_ctrl",
        .chip = NULL,
        .type = "power_ctrl",
        .device_handle = NULL,
        .init = dev_power_ctrl_init,
        .deinit = dev_power_ctrl_deinit
    },
    {
        .next = &g_esp_board_device_handles[3],
        .name = "epaper_ssd16xx",
        .chip = "ssd16xx_290",
        .type = "custom",
        .device_handle = NULL,
        .init = dev_custom_init,
        .deinit = dev_custom_deinit
    },
    {
        .next = &g_esp_board_device_handles[4],
        .name = "rtc_pcf85063a",
        .chip = "pcf85063a",
        .type = "custom",
        .device_handle = NULL,
        .init = dev_custom_init,
        .deinit = dev_custom_deinit
    },
    {
        .next = &g_esp_board_device_handles[5],
        .name = "bat_axp2602",
        .chip = "axp2602",
        .type = "custom",
        .device_handle = NULL,
        .init = dev_custom_init,
        .deinit = dev_custom_deinit
    },
    {
        .next = NULL,
        .name = "radio_lr1121",
        .chip = "lr1121",
        .type = "custom",
        .device_handle = NULL,
        .init = dev_custom_init,
        .deinit = dev_custom_deinit
    },
};
