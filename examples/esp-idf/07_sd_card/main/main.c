#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_epaper.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_board_manager.h"
#include "esp_board_periph.h"
#include "periph_gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

static const char *TAG = "sd_card_test";

/* ESP-IDF task stack sizes are bytes. Independent of a saved main-task config. */
#define SD_TEST_TASK_STACK_BYTES (16U * 1024U)

#define SD_MOUNT_POINT    "/sdcard"
/* Keep both names within FAT 8.3 limits, including when long names are disabled. */
#define SD_TEST_DIRECTORY SD_MOUNT_POINT "/ws_sd"
#define SD_TEST_FILE      SD_TEST_DIRECTORY "/test.txt"

#define BOARD_SD_POWER_PERIPH "gpio_epaper_power"
static periph_gpio_handle_t *s_sd_power;
#define BOARD_SD_CMD_GPIO   GPIO_NUM_10
#define BOARD_SD_D0_GPIO    GPIO_NUM_11
#define BOARD_SD_CLK_GPIO   GPIO_NUM_12

static const char TEST_PAYLOAD[] =
    "Waveshare ESP32-S3 SDMMC round-trip test\n"
    "Only this file is created and removed by example 07.\n";

static bool s_display_ready;
static esp_err_t s_display_result = ESP_OK;
static char s_card_line[40] = "Card: not mounted";
static const char *s_mount_line = "Mount: pending";
static const char *s_io_line = "Read/write: pending";
static const char *s_unmount_line = "Unmount: pending";

static void show_status(const char *title)
{
    if (!s_display_ready || s_display_result != ESP_OK) {
        return;
    }
    const char *lines[] = {s_card_line, s_mount_line, s_io_line, s_unmount_line};
    s_display_result = board_epaper_show_lines(title, lines, 4, false);
    if (s_display_result != ESP_OK) {
        ESP_LOGE(TAG, "display refresh failed: %s; SD test continues on serial",
                 esp_err_to_name(s_display_result));
    }
}

static esp_err_t set_sd_power(bool enabled)
{
    if (!s_sd_power) {
        if (!enabled) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(
                                BOARD_SD_POWER_PERIPH, (void **)&s_sd_power),
                            TAG, "acquire SD power GPIO from Board Manager failed");
    }
    /* Keep this reference so the shared rail stays driven low after the test. */
    ESP_RETURN_ON_ERROR(gpio_set_level(s_sd_power->gpio_num, enabled), TAG,
                        "set SD power GPIO failed");
    ESP_LOGI(TAG, "Board Manager power: %s GPIO%d=%d",
             BOARD_SD_POWER_PERIPH, (int)s_sd_power->gpio_num, enabled);
    if (enabled) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

static esp_err_t list_root_directory(void)
{
    DIR *directory = opendir(SD_MOUNT_POINT);
    if (!directory) {
        ESP_LOGE(TAG, "open root directory failed: errno=%d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Root directory (up to 16 entries):");
    struct dirent *entry;
    unsigned int count = 0;
    while (count < 16 && (entry = readdir(directory)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
        ++count;
    }
    closedir(directory);
    if (count == 0) {
        ESP_LOGI(TAG, "  <empty>");
    }
    return ESP_OK;
}

static esp_err_t write_all(int file, const uint8_t *data, size_t length)
{
    size_t completed = 0;
    while (completed < length) {
        const ssize_t written = write(file, data + completed, length - completed);
        if (written <= 0) {
            return ESP_FAIL;
        }
        completed += (size_t)written;
    }
    return ESP_OK;
}

static esp_err_t run_roundtrip_test(void)
{
    bool directory_created = false;
    bool file_created = false;
    esp_err_t result = ESP_FAIL;

    if (mkdir(SD_TEST_DIRECTORY, 0755) == 0) {
        directory_created = true;
    } else if (errno == EEXIST) {
        struct stat info;
        if (stat(SD_TEST_DIRECTORY, &info) != 0 || !S_ISDIR(info.st_mode)) {
            ESP_LOGE(TAG, "test path exists but is not a directory");
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        ESP_LOGE(TAG, "create test directory failed: errno=%d", errno);
        return ESP_FAIL;
    }

    int file = open(SD_TEST_FILE, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (file < 0) {
        ESP_LOGE(TAG,
                 "refusing to overwrite %s (errno=%d); remove or rename it first",
                 SD_TEST_FILE, errno);
        result = errno == EEXIST ? ESP_ERR_INVALID_STATE : ESP_FAIL;
        goto cleanup;
    }
    file_created = true;

    result = write_all(file, (const uint8_t *)TEST_PAYLOAD,
                       sizeof(TEST_PAYLOAD) - 1);
    if (result == ESP_OK && fsync(file) != 0) {
        result = ESP_FAIL;
    }
    if (close(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "write or synchronize test file failed: errno=%d", errno);
        goto cleanup;
    }

    file = open(SD_TEST_FILE, O_RDONLY);
    if (file < 0) {
        ESP_LOGE(TAG, "open test file for readback failed: errno=%d", errno);
        result = ESP_FAIL;
        goto cleanup;
    }

    uint8_t readback[sizeof(TEST_PAYLOAD)];
    const ssize_t bytes_read = read(file, readback, sizeof(readback));
    const ssize_t trailing = read(file, readback, 1);
    if (close(file) != 0) {
        ESP_LOGE(TAG, "close readback file failed: errno=%d", errno);
        result = ESP_FAIL;
        goto cleanup;
    }
    if (bytes_read != (ssize_t)(sizeof(TEST_PAYLOAD) - 1) || trailing != 0 ||
        memcmp(readback, TEST_PAYLOAD, sizeof(TEST_PAYLOAD) - 1) != 0) {
        ESP_LOGE(TAG, "readback verification failed: bytes=%d trailing=%d",
                 (int)bytes_read, (int)trailing);
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    ESP_LOGI(TAG, "write/read verification passed (%u bytes)",
             (unsigned int)(sizeof(TEST_PAYLOAD) - 1));
    result = ESP_OK;

cleanup:
    if (file_created && unlink(SD_TEST_FILE) != 0) {
        ESP_LOGE(TAG, "remove test file failed: errno=%d", errno);
        result = ESP_FAIL;
    }
    if (directory_created && rmdir(SD_TEST_DIRECTORY) != 0) {
        ESP_LOGE(TAG, "remove test directory failed: errno=%d", errno);
        result = ESP_FAIL;
    }
    return result;
}

static void run_sd_card_test(void)
{
    sdmmc_card_t *card = NULL;
    /* Display initialization must precede mounting: its rollback can cut power. */
    esp_err_t result = esp_board_manager_prepare();
    if (result == ESP_OK) {
        s_display_result = board_epaper_start(NULL);
        s_display_ready = s_display_result == ESP_OK;
        if (!s_display_ready) {
            ESP_LOGE(TAG, "display init failed: %s; SD test continues on serial",
                     esp_err_to_name(s_display_result));
        }
        /* Restore the shared rail even if display initialization rolled back. */
        result = set_sd_power(true);
    }
    if (result != ESP_OK) {
        board_epaper_deinit();
        const esp_err_t power_result = set_sd_power(false);
        if (power_result != ESP_OK) {
            ESP_LOGE(TAG, "SD power rollback failed: %s", esp_err_to_name(power_result));
        }
        ESP_LOGE(TAG, "FINAL: FAIL (%s)", esp_err_to_name(result));
        return;
    }
    show_status("SD TEST");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK_GPIO;
    slot.cmd = BOARD_SD_CMD_GPIO;
    slot.d0 = BOARD_SD_D0_GPIO;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 0,
        .disk_status_check_enable = true,
    };

    ESP_LOGI(TAG, "Mounting FAT card with SDMMC 1-bit: CMD=%d D0=%d CLK=%d",
             BOARD_SD_CMD_GPIO, BOARD_SD_D0_GPIO, BOARD_SD_CLK_GPIO);
    result = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot,
                                     &mount_config, &card);
    if (result != ESP_OK) {
        s_mount_line = "Mount: FAIL (see serial)";
        s_io_line = "Read/write: skipped";
        s_unmount_line = "Unmount: not needed";
        ESP_LOGE(TAG, "mount failed without formatting: %s",
                 esp_err_to_name(result));
        goto power_off;
    }

    sdmmc_card_print_info(stdout, card);
    snprintf(s_card_line, sizeof(s_card_line), "Card: %llu MiB",
             (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024ULL * 1024ULL));
    s_mount_line = "Mount: OK (SDMMC 1-bit)";
    s_io_line = "Read/write: running";
    show_status("SD TEST");
    result = list_root_directory();
    if (result == ESP_OK) {
        result = run_roundtrip_test();
        s_io_line = result == ESP_OK ? "Read/write: PASS" : "Read/write: FAIL";
    } else {
        s_io_line = "Root listing: FAIL";
    }

    {
        const esp_err_t unmount_result =
            esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        card = NULL;
        if (unmount_result != ESP_OK) {
            s_unmount_line = "Unmount: FAIL";
            ESP_LOGE(TAG, "unmount failed: %s", esp_err_to_name(unmount_result));
            result = unmount_result;
        } else {
            s_unmount_line = "Unmount: OK";
            ESP_LOGI(TAG, "filesystem synchronized and unmounted");
        }
    }

power_off:
    /* All filesystem work finishes before any display teardown can cut power. */
    show_status(result == ESP_OK ? "SD PASS" : "SD FAIL");
    if (s_display_ready) {
        const esp_err_t sleep_result = board_epaper_sleep();
        if (sleep_result != ESP_OK) {
            s_display_result = sleep_result;
        }
    }
    {
        const esp_err_t display_cleanup_result = board_epaper_deinit();
        if (display_cleanup_result != ESP_OK) {
            s_display_result = display_cleanup_result;
        }
    }
    {
        const esp_err_t power_result = set_sd_power(false);
        if (power_result != ESP_OK) {
            result = power_result;
        }
    }
    ESP_LOGI(TAG, "DISPLAY: %s (%s)", s_display_result == ESP_OK ? "PASS" : "FAIL",
             esp_err_to_name(s_display_result));
    ESP_LOGI(TAG, "FINAL: %s", result == ESP_OK ? "PASS" : "FAIL");
}

static void sd_card_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "SD/display task stack: %u bytes", SD_TEST_TASK_STACK_BYTES);
    run_sd_card_test();
    ESP_LOGI(TAG, "SD/display minimum free stack: %u bytes",
             (unsigned int)uxTaskGetStackHighWaterMark(NULL));
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Keep LVGL rendering off main even when an old sdkconfig keeps its small stack. */
    ESP_LOGI(TAG, "main stack: %u bytes; starting dedicated SD/display task",
             (unsigned int)CONFIG_ESP_MAIN_TASK_STACK_SIZE);
    if (xTaskCreate(sd_card_task, "sd_card", SD_TEST_TASK_STACK_BYTES, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        /* Never fall back to rendering on main's smaller stack. */
        ESP_LOGE(TAG, "FINAL: FAIL (cannot allocate SD/display task)");
    }
}
