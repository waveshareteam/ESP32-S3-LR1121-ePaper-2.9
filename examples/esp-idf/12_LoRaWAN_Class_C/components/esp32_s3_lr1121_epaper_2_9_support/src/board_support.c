/**
 * @file board_support.c
 * @brief Coordinates board power and the SPI bus shared by display and radio.
 *
 * The Class C runtime keeps the common peripheral rail enabled and serializes
 * every e-paper and LR1121 SPI transaction with one ownership-aware mutex.
 */

#include "board_support.h"

#include <string.h>

#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gen_board_device_custom.h"

#define BOARD_EPAPER_POWER_ON_DELAY_MS 100
#define BOARD_EPAPER_POWER_OFF_DELAY_MS 10
#define BOARD_SHARED_POWER_GPIO GPIO_NUM_38
#define BOARD_SHARED_POWER_ON_LEVEL 1

/* Process-lifetime resources shared by both board device adapters. */
static const char *TAG = "board_support";
static bool s_manager_ready;
static bool s_shared_power_gpio_configured;
static bool s_shared_power_enabled;
static SemaphoreHandle_t s_spi_mutex;
static portMUX_TYPE s_spi_init_lock = portMUX_INITIALIZER_UNLOCKED;
static periph_spi_handle_t *s_spi_handle;
static board_spi_owner_t s_spi_owner = BOARD_SPI_OWNER_NONE;

/* GPIO38 powers both devices, so normal Class C operation never turns it off. */
static esp_err_t board_shared_power_set(bool on)
{
    if (!s_shared_power_gpio_configured) {
        const gpio_config_t power_config = {
            .pin_bit_mask = 1ULL << BOARD_SHARED_POWER_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&power_config), TAG,
                            "configure GPIO38 power failed");
        s_shared_power_gpio_configured = true;
    }

    const bool changed = s_shared_power_enabled != on;
    ESP_RETURN_ON_ERROR(
        gpio_set_level(BOARD_SHARED_POWER_GPIO,
                       on ? BOARD_SHARED_POWER_ON_LEVEL
                          : !BOARD_SHARED_POWER_ON_LEVEL),
        TAG, "set GPIO38 power failed");
    s_shared_power_enabled = on;
    if (changed) {
        vTaskDelay(pdMS_TO_TICKS(on ? BOARD_EPAPER_POWER_ON_DELAY_MS
                                    : BOARD_EPAPER_POWER_OFF_DELAY_MS));
    }
    return ESP_OK;
}

/*
 * Publish exactly one mutex when callers race during startup; losing candidates
 * are deleted after leaving the short critical section.
 */
static esp_err_t spi_mutex_init(void)
{
    if (s_spi_mutex != NULL) {
        return ESP_OK;
    }

    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(candidate != NULL, ESP_ERR_NO_MEM, TAG,
                        "create shared SPI mutex failed");
    taskENTER_CRITICAL(&s_spi_init_lock);
    if (s_spi_mutex == NULL) {
        s_spi_mutex = candidate;
        candidate = NULL;
    }
    taskEXIT_CRITICAL(&s_spi_init_lock);
    if (candidate != NULL) {
        vSemaphoreDelete(candidate);
    }
    return ESP_OK;
}

esp_err_t board_support_prepare(void)
{
    ESP_RETURN_ON_ERROR(board_shared_power_set(true), TAG,
                        "enable GPIO38 peripheral power failed");
    ESP_RETURN_ON_ERROR(spi_mutex_init(), TAG,
                        "create shared SPI mutex failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_spi_mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG,
                        "lock Board Manager initialization failed");

    /*
     * The shared mutex serializes one-time manager initialization. An already
     * initialized manager is an acceptable process-wide state.
     */
    esp_err_t ret = ESP_OK;
    if (!s_manager_ready) {
        ret = esp_board_manager_init();
        if (ret == ESP_BOARD_ERR_MANAGER_ALREADY_INIT) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            s_manager_ready = true;
        }
    }
    if (ret == ESP_OK) {
        ret = board_shared_power_set(true);
    }
    xSemaphoreGive(s_spi_mutex);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "prepare Board Manager failed: %s",
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t get_spi_config(const periph_spi_config_t **config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "SPI config output is null");
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board before SPI config failed");
    /* Both adapters must resolve the same manager-owned SPI bus definition. */
    return esp_board_manager_get_periph_config(BOARD_PERIPH_SHARED_SPI,
                                               (void **)config);
}

esp_err_t board_epaper_power_set(bool on)
{
    if (!on) {
        /*
         * GPIO38 also powers LR1121. The Class C example keeps this rail on;
         * panel sleep, if ever requested, only sends the panel sleep command.
         */
        ESP_LOGD(TAG,
                 "e-paper power-off skipped because GPIO38 is shared "
                 "with LR1121");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board before power control failed");
    ESP_RETURN_ON_ERROR(
        esp_board_device_power_ctrl(BOARD_DEVICE_EPAPER, true), TAG,
        "set e-paper power failed");
    return board_shared_power_set(true);
}

esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "e-paper config output is null");
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board before e-paper config failed");

    const dev_custom_epaper_ssd16xx_config_t *device_config = NULL;
    const periph_spi_config_t *spi_config = NULL;
    ESP_RETURN_ON_ERROR(
        esp_board_manager_get_device_config(BOARD_DEVICE_EPAPER,
                                            (void **)&device_config),
        TAG, "get e-paper config failed");
    ESP_RETURN_ON_ERROR(get_spi_config(&spi_config), TAG,
                        "get shared SPI config failed");
    /* Reject a device accidentally bound to a different peripheral. */
    ESP_RETURN_ON_FALSE(
        device_config != NULL && spi_config != NULL &&
            device_config->peripheral_count == 1 &&
            device_config->peripheral_name != NULL &&
            strcmp(device_config->peripheral_name,
                   BOARD_PERIPH_SHARED_SPI) == 0,
        ESP_ERR_INVALID_STATE, TAG, "invalid e-paper board config");

    *config = (board_epaper_hw_config_t){
        .busy = (gpio_num_t)device_config->busy_gpio,
        .reset = (gpio_num_t)device_config->reset_gpio,
        .dc = (gpio_num_t)device_config->dc_gpio,
        .cs = (gpio_num_t)device_config->cs_gpio,
        .sck = (gpio_num_t)spi_config->spi_bus_config.sclk_io_num,
        .mosi = (gpio_num_t)spi_config->spi_bus_config.mosi_io_num,
        .spi_host = spi_config->spi_port,
        .spi_speed_hz = device_config->spi_speed_hz,
    };
    return ESP_OK;
}

esp_err_t board_radio_get_hw_config(board_radio_hw_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "radio config output is null");
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board before LR1121 config failed");

    const dev_custom_radio_lr1121_config_t *device_config = NULL;
    const periph_spi_config_t *spi_config = NULL;
    ESP_RETURN_ON_ERROR(
        esp_board_manager_get_device_config(BOARD_DEVICE_RADIO,
                                            (void **)&device_config),
        TAG, "get LR1121 config failed");
    ESP_RETURN_ON_ERROR(get_spi_config(&spi_config), TAG,
                        "get shared SPI config failed");
    /* Reject a device accidentally bound to a different peripheral. */
    ESP_RETURN_ON_FALSE(
        device_config != NULL && spi_config != NULL &&
            device_config->peripheral_count == 1 &&
            device_config->peripheral_name != NULL &&
            strcmp(device_config->peripheral_name,
                   BOARD_PERIPH_SHARED_SPI) == 0,
        ESP_ERR_INVALID_STATE, TAG, "invalid LR1121 board config");

    *config = (board_radio_hw_config_t){
        .dio1 = (gpio_num_t)device_config->dio1_gpio,
        .nss = (gpio_num_t)device_config->nss_gpio,
        .reset = (gpio_num_t)device_config->reset_gpio,
        .busy = (gpio_num_t)device_config->busy_gpio,
        .dio7 = (gpio_num_t)device_config->dio7_gpio,
        .dio8 = (gpio_num_t)device_config->dio8_gpio,
        .sck = (gpio_num_t)spi_config->spi_bus_config.sclk_io_num,
        .miso = (gpio_num_t)spi_config->spi_bus_config.miso_io_num,
        .mosi = (gpio_num_t)spi_config->spi_bus_config.mosi_io_num,
        .spi_host = spi_config->spi_port,
        .spi_speed_hz = device_config->spi_speed_hz,
    };
    return ESP_OK;
}

bool board_spi_lock(board_spi_owner_t owner, int32_t timeout_ms)
{
    if (owner == BOARD_SPI_OWNER_NONE ||
        board_support_prepare() != ESP_OK) {
        return false;
    }

    TickType_t ticks = portMAX_DELAY;
    if (timeout_ms >= 0) {
        ticks = pdMS_TO_TICKS((uint32_t)timeout_ms);
    }
    if (xSemaphoreTake(s_spi_mutex, ticks) != pdTRUE) {
        return false;
    }

    /*
     * Keep one Board Manager reference for the process lifetime so neither
     * client can tear down the bus while the other is active. Resolve it only
     * while holding the mutex used for all subsequent transactions.
     */
    if (s_spi_handle == NULL) {
        const esp_err_t ret = esp_board_periph_ref_handle(
            BOARD_PERIPH_SHARED_SPI, (void **)&s_spi_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reference shared SPI failed: %s",
                     esp_err_to_name(ret));
            xSemaphoreGive(s_spi_mutex);
            return false;
        }
    }
    s_spi_owner = owner;
    return true;
}

void board_spi_unlock(board_spi_owner_t owner)
{
    if (s_spi_mutex == NULL) {
        return;
    }
    if (s_spi_owner != owner) {
        /* Never release another client's transaction on an ownership error. */
        ESP_LOGW(TAG, "SPI unlock owner mismatch: current=%d request=%d",
                 (int)s_spi_owner, (int)owner);
        return;
    }
    s_spi_owner = BOARD_SPI_OWNER_NONE;
    xSemaphoreGive(s_spi_mutex);
}

board_spi_owner_t board_spi_get_owner(void)
{
    return s_spi_owner;
}
