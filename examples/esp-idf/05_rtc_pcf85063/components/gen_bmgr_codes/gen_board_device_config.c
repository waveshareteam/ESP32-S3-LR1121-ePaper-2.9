/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "dev_custom.h"
#include "dev_led_strip.h"
#include "dev_power_ctrl.h"
#include "gen_board_device_custom.h"

// Device configuration structures
const static dev_led_strip_config_t esp_bmgr_status_led_cfg = {
    .name = "status_led",
    .chip = "ws2812",
    .sub_type = "rmt",
    .strip_config = {
        .strip_gpio_num = 21,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    },
    .sub_cfg = {
        .rmt = {
            .rmt_config = {
                .clk_src = RMT_CLK_SRC_DEFAULT,
                .resolution_hz = 10000000,
                .mem_block_symbols = 0,
                .flags = {
                    .with_dma = false,
                },
            },
        },
    },
};

const static dev_power_ctrl_config_t esp_bmgr_epaper_power_ctrl_cfg = {
    .name = "epaper_power_ctrl",
    .sub_type = "gpio",
    .sub_cfg = {
        .gpio = {
            .gpio_name = "gpio_epaper_power",
            .active_level = 1,
        },
    },
};

const static dev_custom_epaper_ssd16xx_config_t esp_bmgr_epaper_ssd16xx_cfg = {
    .name = "epaper_ssd16xx",
    .type = "custom",
    .chip = "ssd16xx_290",
    .busy_gpio = 8,
    .reset_gpio = 9,
    .dc_gpio = 17,
    .cs_gpio = 18,
    .spi_speed_hz = 10000000,
    .peripheral_count = 1,
    .peripheral_name = "spi_shared",
};

const static dev_custom_rtc_pcf85063a_config_t esp_bmgr_rtc_pcf85063a_cfg = {
    .name = "rtc_pcf85063a",
    .type = "custom",
    .chip = "pcf85063a",
    .address = 81,
    .frequency = 400000,
    .int_gpio = 7,
    .peripheral_count = 1,
    .peripheral_name = "i2c_master",
};

const static dev_custom_bat_axp2602_config_t esp_bmgr_bat_axp2602_cfg = {
    .name = "bat_axp2602",
    .type = "custom",
    .chip = "axp2602",
    .address = 98,
    .frequency = 400000,
    .sense_resistor_mohm = 10,
    .idle_current_threshold_ma = 1,
    .peripheral_count = 1,
    .peripheral_name = "i2c_master",
};

const static dev_custom_radio_lr1121_config_t esp_bmgr_radio_lr1121_cfg = {
    .name = "radio_lr1121",
    .type = "custom",
    .chip = "lr1121",
    .dio1_gpio = 6,
    .nss_gpio = 42,
    .reset_gpio = 39,
    .busy_gpio = 41,
    .dio7_gpio = -1,
    .dio8_gpio = -1,
    .spi_speed_hz = 8000000,
    .peripheral_count = 1,
    .peripheral_name = "spi_shared",
};

// Device descriptor array

const esp_board_device_desc_t g_esp_board_devices[] = {
    {
        .next = &g_esp_board_devices[1],
        .name = "status_led",
        .chip = "ws2812",
        .type = "led_strip",
        .sub_type = "rmt",
        .cfg = &esp_bmgr_status_led_cfg,
        .cfg_size = sizeof(esp_bmgr_status_led_cfg),
        .init_skip = true,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[2],
        .name = "epaper_power_ctrl",
        .chip = NULL,
        .type = "power_ctrl",
        .sub_type = "gpio",
        .cfg = &esp_bmgr_epaper_power_ctrl_cfg,
        .cfg_size = sizeof(esp_bmgr_epaper_power_ctrl_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[3],
        .name = "epaper_ssd16xx",
        .chip = "ssd16xx_290",
        .type = "custom",
        .sub_type = NULL,
        .cfg = &esp_bmgr_epaper_ssd16xx_cfg,
        .cfg_size = sizeof(esp_bmgr_epaper_ssd16xx_cfg),
        .init_skip = true,
        .power_ctrl_device = "epaper_power_ctrl",
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[4],
        .name = "rtc_pcf85063a",
        .chip = "pcf85063a",
        .type = "custom",
        .sub_type = NULL,
        .cfg = &esp_bmgr_rtc_pcf85063a_cfg,
        .cfg_size = sizeof(esp_bmgr_rtc_pcf85063a_cfg),
        .init_skip = true,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[5],
        .name = "bat_axp2602",
        .chip = "axp2602",
        .type = "custom",
        .sub_type = NULL,
        .cfg = &esp_bmgr_bat_axp2602_cfg,
        .cfg_size = sizeof(esp_bmgr_bat_axp2602_cfg),
        .init_skip = true,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = NULL,
        .name = "radio_lr1121",
        .chip = "lr1121",
        .type = "custom",
        .sub_type = NULL,
        .cfg = &esp_bmgr_radio_lr1121_cfg,
        .cfg_size = sizeof(esp_bmgr_radio_lr1121_cfg),
        .init_skip = true,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
};
