/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "hal/gpio_types.h"
#include "periph_gpio.h"
#include "periph_spi.h"

// Peripheral configuration structures
const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 13,
    .scl_io_num = 14,
    .glitch_ignore_cnt = 7,
    .intr_priority = 1,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = true,
    },
    .clk_source = I2C_CLK_SRC_DEFAULT,
};

static periph_spi_config_t esp_bmgr_spi_shared_cfg = {
    .spi_port = SPI2_HOST,
    .spi_bus_config = {
        .mosi_io_num = 45,
        .miso_io_num = 46,
        .sclk_io_num = 40,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .data_io_default_level = false,
        .max_transfer_sz = 4096,
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
        .dma_burst_size = 0,
    },
};

const static periph_gpio_config_t esp_bmgr_gpio_boot_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 1,
};

const static periph_gpio_config_t esp_bmgr_gpio_epaper_power_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(38),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 1,
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2c_master",
        .type = "i2c",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2c_master_cfg,
        .cfg_size = sizeof(esp_bmgr_i2c_master_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[2],
        .name = "spi_shared",
        .type = "spi",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_spi_shared_cfg,
        .cfg_size = sizeof(esp_bmgr_spi_shared_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[3],
        .name = "gpio_boot",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_boot_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_boot_cfg),
        .id = 0,
    },
    {
        .next = NULL,
        .name = "gpio_epaper_power",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_epaper_power_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_epaper_power_cfg),
        .id = 0,
    },
};
