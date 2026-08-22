#include "board_support.h"

#include <string.h>

#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gen_board_device_custom.h"

/*
 * Central board contract for the LR1121 and e-paper devices. Both devices use
 * the same power rail and SPI bus, so this module obtains the generated
 * Board Manager configuration and provides one cross-driver bus lock.
 */

#define BOARD_SHARED_POWER_GPIO        GPIO_NUM_38
#define BOARD_SHARED_POWER_ON_LEVEL    1
#define BOARD_POWER_STABILIZE_DELAY_MS 100




/* Process-wide state is protected by the SPI mutex after one-time creation. */
static const char *TAG = "board_support";
static bool s_board_manager_ready;
static bool s_shared_power_gpio_configured;
static bool s_shared_power_enabled;
static bool s_shared_power_log_emitted;
static SemaphoreHandle_t s_spi_mutex;
static portMUX_TYPE s_spi_init_lock = portMUX_INITIALIZER_UNLOCKED;
static periph_spi_handle_t *s_spi_handle;
static board_spi_owner_t s_spi_owner = BOARD_SPI_OWNER_NONE;

/* Enables the shared rail once and waits for LR1121/panel stabilization. */
static esp_err_t board_shared_power_enable(void)
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
                            "configure shared power GPIO failed");
        s_shared_power_gpio_configured = true;
    }

    const bool needs_stabilize = !s_shared_power_enabled;
    ESP_RETURN_ON_ERROR(gpio_set_level(BOARD_SHARED_POWER_GPIO,
                                       BOARD_SHARED_POWER_ON_LEVEL),
                        TAG, "enable shared radio/display power failed");
    s_shared_power_enabled = true;

    if (needs_stabilize) {
        if (!s_shared_power_log_emitted) {
            ESP_LOGI(TAG, "Shared LR1121/e-paper power enabled: GPIO%d=%d",
                     BOARD_SHARED_POWER_GPIO, BOARD_SHARED_POWER_ON_LEVEL);
            s_shared_power_log_emitted = true;
        }
        vTaskDelay(pdMS_TO_TICKS(BOARD_POWER_STABILIZE_DELAY_MS));
    }
    return ESP_OK;
}

/* Uses a short critical section only to publish the newly created mutex. */
static esp_err_t spi_mutex_init(void)
{
    if (s_spi_mutex) {
        return ESP_OK;
    }

    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(candidate, ESP_ERR_NO_MEM, TAG,
                        "create shared SPI mutex failed");
    taskENTER_CRITICAL(&s_spi_init_lock);
    if (!s_spi_mutex) {
        s_spi_mutex = candidate;
        candidate = NULL;
    }
    taskEXIT_CRITICAL(&s_spi_init_lock);
    if (candidate) {
        vSemaphoreDelete(candidate);
    }
    return ESP_OK;
}

static esp_err_t get_spi_config(const periph_spi_config_t **config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG,
                        "SPI config output is null");
    return esp_board_manager_get_periph_config(BOARD_PERIPH_SHARED_SPI,
                                               (void **)config);
}


/* Board Manager and power initialization are idempotent for all consumers. */
esp_err_t board_support_prepare(void)
{
    ESP_RETURN_ON_ERROR(spi_mutex_init(), TAG, "create shared SPI mutex failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_spi_mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "lock board support init failed");

    esp_err_t ret = ESP_OK;
    if (!s_board_manager_ready) {
        ret = esp_board_manager_init();
        if (ret == ESP_BOARD_ERR_MANAGER_ALREADY_INIT) {
            ret = ESP_OK;
        }
        if (ret == ESP_OK) {
            s_board_manager_ready = true;
        }
    }
    if (ret == ESP_OK) {
        ret = board_shared_power_enable();
    }
    xSemaphoreGive(s_spi_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "prepare Board Manager/shared power failed: %s",
                 esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t board_epaper_power_set(bool on)
{
    if (on) {
        return board_shared_power_enable();
    }

    /*
     * GPIO38 also powers LR1121. Keep the rail asserted while Class C is active;
     * board_epaper_sleep() still sends the panel's own sleep command first.
     */
    ESP_LOGD(TAG, "e-paper power-off skipped because GPIO38 is shared with LR1121");
    return ESP_OK;
}

esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG,
                        "e-paper config output is null");
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board support failed");

    const dev_custom_epaper_ssd16xx_config_t *device_config = NULL;
    const periph_spi_config_t *spi_config = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(BOARD_DEVICE_EPAPER,
                                                            (void **)&device_config),
                        TAG, "get e-paper config failed");
    ESP_RETURN_ON_ERROR(get_spi_config(&spi_config), TAG,
                        "get shared SPI config failed");
    /* Reject a device accidentally bound to a different peripheral. */
    ESP_RETURN_ON_FALSE(
        device_config &&
            device_config->peripheral_count == 1 &&
            device_config->peripheral_name &&
            strcmp(device_config->peripheral_name,
                   BOARD_PERIPH_SHARED_SPI) == 0,
        ESP_ERR_INVALID_STATE, TAG,
                        "invalid e-paper board config");

    *config = (board_epaper_hw_config_t) {
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
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG,
                        "radio config output is null");
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board support failed");

    const dev_custom_radio_lr1121_config_t *device_config = NULL;
    const periph_spi_config_t *spi_config = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(BOARD_DEVICE_RADIO,
                                                            (void **)&device_config),
                        TAG, "get LR1121 config failed");
    ESP_RETURN_ON_ERROR(get_spi_config(&spi_config), TAG,
                        "get shared SPI config failed");
    /* Reject a device accidentally bound to a different peripheral. */
    ESP_RETURN_ON_FALSE(
        device_config &&
            device_config->peripheral_count == 1 &&
            device_config->peripheral_name &&
            strcmp(device_config->peripheral_name,
                   BOARD_PERIPH_SHARED_SPI) == 0,
        ESP_ERR_INVALID_STATE, TAG,
                        "invalid LR1121 board config");

    *config = (board_radio_hw_config_t) {
        .dio1 = (gpio_num_t)device_config->dio1_gpio,
        .nss = (gpio_num_t)device_config->nss_gpio,
        .reset = (gpio_num_t)device_config->reset_gpio,
        .busy = (gpio_num_t)device_config->busy_gpio,
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
    if (owner == BOARD_SPI_OWNER_NONE || spi_mutex_init() != ESP_OK) {
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
     * Referencing the managed peripheral keeps its bus lifetime valid while
     * radio and panel device handles use it.
     */
    if (!s_spi_handle) {
        esp_err_t ret = esp_board_periph_ref_handle(BOARD_PERIPH_SHARED_SPI,
                                                    (void **)&s_spi_handle);
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
    if (!s_spi_mutex) {
        return;
    }
    if (s_spi_owner != owner) {
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
