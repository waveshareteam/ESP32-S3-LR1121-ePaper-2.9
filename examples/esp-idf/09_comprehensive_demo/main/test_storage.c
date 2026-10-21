#include "app_tests.h"

#include <inttypes.h>
#include <stdio.h>

#include "board_support.h"
#include "driver/sdmmc_host.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage_test";

#define SD_MOUNT_POINT       "/sdcard"
#define SD_TEST_BYTES        (1024U * 1024U)
#define SD_READ_BUFFER_BYTES (32U * 1024U)

static sdmmc_card_t *s_card;

static esp_err_t read_card_prefix(uint32_t *bytes_read, uint32_t *crc,
                                  uint32_t *throughput_kib_s)
{
    const size_t sector_size = (size_t)s_card->csd.sector_size;
    if (sector_size == 0 || sector_size > SD_READ_BUFFER_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint64_t card_bytes =
        (uint64_t)s_card->csd.capacity * sector_size;
    const size_t target_bytes =
        card_bytes < SD_TEST_BYTES ? (size_t)card_bytes : SD_TEST_BYTES;
    const size_t target_sectors = target_bytes / sector_size;
    const size_t chunk_sectors = SD_READ_BUFFER_BYTES / sector_size;
    uint8_t *buffer = heap_caps_malloc(
        chunk_sectors * sector_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t aggregate = 0;
    size_t completed = 0;
    const int64_t start = esp_timer_get_time();
    while (completed < target_sectors) {
        size_t sectors = target_sectors - completed;
        if (sectors > chunk_sectors) {
            sectors = chunk_sectors;
        }
        const esp_err_t ret =
            sdmmc_read_sectors(s_card, buffer, completed, sectors);
        if (ret != ESP_OK) {
            heap_caps_free(buffer);
            return ret;
        }
        const size_t chunk_bytes = sectors * sector_size;
        aggregate = esp_crc32_le(aggregate, buffer, chunk_bytes);
        completed += sectors;
        vTaskDelay(1);
    }
    const int64_t elapsed_us = esp_timer_get_time() - start;
    heap_caps_free(buffer);

    *bytes_read = (uint32_t)(completed * sector_size);
    *crc = aggregate;
    if (elapsed_us > 0) {
        *throughput_kib_s = (uint32_t)(
            ((uint64_t)*bytes_read * 1000000U) /
            ((uint64_t)elapsed_us * 1024U));
    }
    return ESP_OK;
}

esp_err_t test_storage_run(app_test_result_t *result)
{
    if (s_card) {
        app_result_set(result, "SD", APP_TEST_PASS,
                       "SDMMC already mounted", "READ_ONLY");
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SDMMC_CLK_GPIO;
    slot.cmd = BOARD_SDMMC_CMD_GPIO;
    slot.d0 = BOARD_SDMMC_D0_GPIO;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 0,
        .disk_status_check_enable = true,
    };
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (ret != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG,
                 "SD card not mounted; no format/write attempted: %s",
                 esp_err_to_name(ret));
        app_result_set(result, "SD", APP_TEST_NOT_TESTED,
                       "SD NOT_TESTED", "insert FAT card");
        return ESP_OK;
    }

    sdmmc_card_print_info(stdout, s_card);
    uint32_t bytes_read = 0;
    uint32_t crc = 0;
    uint32_t throughput = 0;
    ret = read_card_prefix(&bytes_read, &crc, &throughput);
    if (ret != ESP_OK) {
        app_result_set(result, "SD", APP_TEST_FAIL,
                       "SD read failed", esp_err_to_name(ret));
        (void)app_storage_stop();
        return ret;
    }

    ESP_LOGI(TAG,
             "SDMMC 1-bit READ_ONLY bytes=%" PRIu32 " crc=%08" PRIX32
             " throughput=%" PRIu32 "KiB/s pins CMD%d D0%d CLK%d",
             bytes_read, crc, throughput, BOARD_SDMMC_CMD_GPIO,
             BOARD_SDMMC_D0_GPIO, BOARD_SDMMC_CLK_GPIO);
    char line1[48];
    char line2[48];
    snprintf(line1, sizeof(line1), "SD 1bit %" PRIu32 "K PASS",
             bytes_read / 1024U);
    snprintf(line2, sizeof(line2), "%" PRIu32 "KiB/s CRC%08" PRIX32,
             throughput, crc);
    app_result_set(result, "SD", APP_TEST_PASS, line1, line2);
    return ESP_OK;
}

esp_err_t app_storage_stop(void)
{
    if (!s_card) {
        return ESP_OK;
    }
    const esp_err_t ret =
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    if (ret == ESP_OK) {
        s_card = NULL;
        ESP_LOGI(TAG, "SD filesystem synchronized and unmounted");
    }
    return ret;
}
