#include "board_support.h"

#include <stdlib.h>
#include <string.h>

#include "esp_board_manager_includes.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gen_board_device_custom.h"
#include "pcf85063a.h"

#define BOARD_I2C_TIMEOUT_MS 100
#define BOARD_EPAPER_POWER_ON_DELAY_MS 100
#define BOARD_EPAPER_POWER_OFF_DELAY_MS 10
#define BOARD_SHARED_POWER_GPIO GPIO_NUM_38
#define PCF85063_TIME_LEN 7
#define BOARD_STATUS_LED_MAX_BRIGHTNESS 48

typedef struct {
    pcf85063a_dev_t device;
    const char *bus_name;
    gpio_num_t int_gpio;
    volatile uint32_t interrupt_count;
    bool bus_referenced;
    bool isr_added;
} board_rtc_device_t;

static const char *TAG = "board_support";
static bool s_ops_registered;
/* Process-wide service: keep it across device teardown/reinitialization. */
static bool s_gpio_isr_service_ready;
static bool s_epaper_power_enabled;
static i2c_master_bus_handle_t s_i2c_bus;
static dev_led_strip_handles_t *s_status_led;
static board_rtc_device_t *s_rtc;
static periph_gpio_handle_t *s_boot_handle;
static SemaphoreHandle_t s_spi_mutex;
static portMUX_TYPE s_spi_init_lock = portMUX_INITIALIZER_UNLOCKED;
static periph_spi_handle_t *s_spi_handle;
static board_spi_owner_t s_spi_owner = BOARD_SPI_OWNER_NONE;

static esp_err_t get_spi_config(const periph_spi_config_t **config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "SPI config output is null");
    return esp_board_manager_get_periph_config(BOARD_PERIPH_SHARED_SPI, (void **)config);
}

esp_err_t board_i2c_acquire(i2c_master_bus_handle_t *bus)
{
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_ARG, TAG, "I2C handle output is null");
    if (!s_i2c_bus) {
        ESP_RETURN_ON_ERROR(esp_board_periph_ref_handle(BOARD_PERIPH_I2C,
                                                       (void **)&s_i2c_bus),
                            TAG, "reference I2C peripheral failed");
    }
    *bus = s_i2c_bus;
    return ESP_OK;
}

esp_err_t board_i2c_get_config(const i2c_master_bus_config_t **config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "I2C config output is null");
    return esp_board_manager_get_periph_config(BOARD_PERIPH_I2C, (void **)config);
}

esp_err_t board_i2c_probe(uint8_t address, uint32_t timeout_ms)
{
    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(board_i2c_acquire(&bus), TAG, "acquire I2C failed");
    return i2c_master_probe(bus, address, (int)timeout_ms);
}

gpio_num_t board_boot_gpio(void)
{
    if (!s_boot_handle) {
        esp_err_t ret = esp_board_periph_ref_handle(BOARD_PERIPH_BOOT,
                                                    (void **)&s_boot_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reference BOOT GPIO failed: %s", esp_err_to_name(ret));
            return GPIO_NUM_NC;
        }
    }
    return s_boot_handle->gpio_num;
}

static esp_err_t status_led_acquire(led_strip_handle_t *strip)
{
    ESP_RETURN_ON_FALSE(strip, ESP_ERR_INVALID_ARG, TAG, "status LED output is null");
    if (!s_status_led) {
        ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(BOARD_DEVICE_STATUS_LED),
                            TAG, "initialize status LED failed");
        esp_err_t ret = esp_board_manager_get_device_handle(BOARD_DEVICE_STATUS_LED,
                                                            (void **)&s_status_led);
        if (ret != ESP_OK || !s_status_led || !s_status_led->strip_handle) {
            if (ret == ESP_OK) {
                ret = ESP_ERR_INVALID_STATE;
            }
            esp_board_manager_deinit_device_by_name(BOARD_DEVICE_STATUS_LED);
            s_status_led = NULL;
            return ret;
        }
    }
    *strip = s_status_led->strip_handle;
    return ESP_OK;
}

static esp_err_t status_led_clear(void)
{
    if (!s_status_led || !s_status_led->strip_handle) {
        return ESP_OK;
    }
    return led_strip_clear(s_status_led->strip_handle);
}

esp_err_t board_status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if ((red | green | blue) == 0U) {
        return status_led_clear();
    }

    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board support for status LED failed");
    led_strip_handle_t strip = NULL;
    ESP_RETURN_ON_ERROR(status_led_acquire(&strip), TAG,
                        "acquire status LED failed");
    ESP_RETURN_ON_ERROR(led_strip_set_pixel(strip, 0, red, green, blue), TAG,
                        "set status LED pixel failed");
    return led_strip_refresh(strip);
}

esp_err_t board_status_led_set_percent(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "status LED percentage must be 0..100");
    if (percent == 0U) {
        return status_led_clear();
    }
    const uint8_t level = (uint8_t)(((uint16_t)BOARD_STATUS_LED_MAX_BRIGHTNESS *
                                     percent + 99U) / 100U);
    return board_status_led_set_rgb(level, level, level);
}

esp_err_t board_status_led_set(bool on)
{
    return on ? board_status_led_set_rgb(BOARD_STATUS_LED_MAX_BRIGHTNESS,
                                         BOARD_STATUS_LED_MAX_BRIGHTNESS,
                                         BOARD_STATUS_LED_MAX_BRIGHTNESS)
              : status_led_clear();
}

esp_err_t board_epaper_power_set(bool on)
{
    if (s_epaper_power_enabled == on) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_board_device_power_ctrl(BOARD_DEVICE_EPAPER, on),
                        TAG, "set e-paper power failed");
    s_epaper_power_enabled = on;
    vTaskDelay(pdMS_TO_TICKS(on ? BOARD_EPAPER_POWER_ON_DELAY_MS
                                : BOARD_EPAPER_POWER_OFF_DELAY_MS));
    return ESP_OK;
}

esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "e-paper config output is null");

    const dev_custom_epaper_ssd16xx_config_t *device_config = NULL;
    const periph_spi_config_t *spi_config = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(BOARD_DEVICE_EPAPER,
                                                            (void **)&device_config),
                        TAG, "get e-paper config failed");
    ESP_RETURN_ON_ERROR(get_spi_config(&spi_config), TAG, "get shared SPI config failed");
    ESP_RETURN_ON_FALSE(device_config && spi_config &&
                        device_config->peripheral_count == 1 &&
                        device_config->peripheral_name &&
                        strcmp(device_config->peripheral_name, BOARD_PERIPH_SHARED_SPI) == 0,
                        ESP_ERR_INVALID_STATE, TAG, "invalid e-paper board config");

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
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "radio config output is null");

    const dev_custom_radio_lr1121_config_t *device_config = NULL;
    const periph_spi_config_t *spi_config = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(BOARD_DEVICE_RADIO,
                                                            (void **)&device_config),
                        TAG, "get LR1121 config failed");
    ESP_RETURN_ON_ERROR(get_spi_config(&spi_config), TAG, "get shared SPI config failed");
    ESP_RETURN_ON_FALSE(device_config && spi_config &&
                        device_config->peripheral_count == 1 &&
                        device_config->peripheral_name &&
                        strcmp(device_config->peripheral_name, BOARD_PERIPH_SHARED_SPI) == 0,
                        ESP_ERR_INVALID_STATE, TAG, "invalid LR1121 board config");

    *config = (board_radio_hw_config_t) {
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

static esp_err_t spi_mutex_init(void)
{
    if (s_spi_mutex) {
        return ESP_OK;
    }

    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(candidate, ESP_ERR_NO_MEM, TAG, "create SPI mutex failed");
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

    if (!s_spi_handle) {
        esp_err_t ret = esp_board_periph_ref_handle(BOARD_PERIPH_SHARED_SPI,
                                                    (void **)&s_spi_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reference shared SPI failed: %s", esp_err_to_name(ret));
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

static void IRAM_ATTR rtc_interrupt_handler(void *arg)
{
    board_rtc_device_t *handle = (board_rtc_device_t *)arg;
    handle->interrupt_count++;
}

static int rtc_device_init(void *raw_config, int config_size, void **device_handle)
{
    if (!raw_config || !device_handle ||
        config_size != sizeof(dev_custom_rtc_pcf85063a_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }

    const dev_custom_rtc_pcf85063a_config_t *config = raw_config;
    if (config->address != PCF85063A_ADDRESS || config->frequency != 400000 ||
        config->peripheral_count != 1 || !config->peripheral_name ||
        strcmp(config->peripheral_name, BOARD_PERIPH_I2C) != 0 ||
        !GPIO_IS_VALID_GPIO(config->int_gpio)) {
        return ESP_ERR_INVALID_ARG;
    }

    board_rtc_device_t *handle = calloc(1, sizeof(*handle));
    if (!handle) {
        return ESP_ERR_NO_MEM;
    }
    handle->bus_name = config->peripheral_name;
    handle->int_gpio = (gpio_num_t)config->int_gpio;

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t ret = esp_board_periph_ref_handle(handle->bus_name, (void **)&bus);
    if (ret != ESP_OK) {
        goto fail;
    }
    handle->bus_referenced = true;
    ret = i2c_master_probe(bus, (uint8_t)config->address, BOARD_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        goto fail;
    }
    ret = pcf85063a_init(&handle->device, bus, (uint8_t)config->address);
    if (ret != ESP_OK) {
        goto fail;
    }

    const gpio_config_t int_gpio_config = {
        .pin_bit_mask = BIT64(handle->int_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ret = gpio_config(&int_gpio_config);
    if (ret != ESP_OK) {
        goto fail;
    }
    /* board_support_prepare() owns the shared GPIO ISR service. */
    ret = gpio_isr_handler_add(handle->int_gpio, rtc_interrupt_handler, handle);
    if (ret != ESP_OK) {
        goto fail;
    }
    handle->isr_added = true;
    *device_handle = handle;
    return ESP_OK;

fail:
    if (handle->isr_added) {
        gpio_isr_handler_remove(handle->int_gpio);
    }
    if (handle->device.dev_handle) {
        i2c_master_bus_rm_device(handle->device.dev_handle);
    }
    if (handle->bus_referenced) {
        if (esp_board_periph_unref_handle(handle->bus_name) == ESP_OK) {
            handle->bus_referenced = false;
        }
    }
    free(handle);
    return ret;
}

static int rtc_device_deinit(void *device_handle)
{
    board_rtc_device_t *handle = device_handle;
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->isr_added) {
        esp_err_t ret = gpio_isr_handler_remove(handle->int_gpio);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "remove RTC ISR failed during cleanup: %s",
                     esp_err_to_name(ret));
            return ret;
        }
        handle->isr_added = false;
    }
    esp_err_t ret = gpio_reset_pin(handle->int_gpio);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "reset RTC INT GPIO failed during cleanup: %s",
                 esp_err_to_name(ret));
    }
    if (handle->device.dev_handle) {
        ret = i2c_master_bus_rm_device(handle->device.dev_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "remove RTC I2C device failed during cleanup: %s",
                     esp_err_to_name(ret));
            return ret;
        }
        handle->device.dev_handle = NULL;
    }
    if (handle->bus_referenced) {
        ret = esp_board_periph_unref_handle(handle->bus_name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "release RTC I2C bus failed during cleanup: %s",
                     esp_err_to_name(ret));
            return ret;
        }
        handle->bus_referenced = false;
    }
    if (s_rtc == handle) {
        s_rtc = NULL;
    }
    free(handle);
    return ESP_OK;
}

esp_err_t board_support_prepare(void)
{
    ESP_RETURN_ON_ERROR(spi_mutex_init(), TAG, "create shared SPI mutex failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_spi_mutex, portMAX_DELAY) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "lock board support init failed");

    esp_err_t ret = ESP_OK;
    if (!s_gpio_isr_service_ready) {
        /* All device handlers share this service; flags=0 matches RTC/LoRa. */
        ret = gpio_install_isr_service(0);
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            s_gpio_isr_service_ready = true;
            ret = ESP_OK;
        }
    }
    if (ret == ESP_OK && !s_ops_registered) {
        ret = esp_board_device_set_ops(BOARD_DEVICE_RTC,
                                       rtc_device_init, rtc_device_deinit);
        if (ret == ESP_OK) {
            s_ops_registered = true;
        }
    }
    if (ret == ESP_OK) {
        ret = esp_board_manager_init();
        if (ret == ESP_BOARD_ERR_MANAGER_ALREADY_INIT) {
            ret = ESP_OK;
        }
    }
    xSemaphoreGive(s_spi_mutex);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "prepare Board Manager failed: %s",
                 esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t board_support_deinit(void)
{
    if (!s_spi_mutex) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(1000)) == pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "lock board support deinit failed");
    if (s_spi_owner != BOARD_SPI_OWNER_NONE) {
        xSemaphoreGive(s_spi_mutex);
        ESP_LOGE(TAG, "shared SPI still owned by %d", (int)s_spi_owner);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_board_manager_deinit();
    if (ret == ESP_BOARD_ERR_MANAGER_NOT_INIT) {
        ret = ESP_OK;
    }
    if (ret != ESP_OK) {
        xSemaphoreGive(s_spi_mutex);
        ESP_LOGE(TAG, "deinitialize Board Manager failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    SemaphoreHandle_t mutex = s_spi_mutex;
    s_i2c_bus = NULL;
    s_status_led = NULL;
    s_rtc = NULL;
    s_boot_handle = NULL;
    s_spi_handle = NULL;
    s_spi_owner = BOARD_SPI_OWNER_NONE;
    s_ops_registered = false;
    s_epaper_power_enabled = false;
    s_spi_mutex = NULL;
    xSemaphoreGive(mutex);
    vSemaphoreDelete(mutex);
    ESP_LOGI(TAG, "Board Manager devices and peripherals released");
    return ESP_OK;
}

esp_err_t board_rtc_init(void)
{
    if (s_rtc) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG, "prepare board support failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(BOARD_DEVICE_RTC),
                        TAG, "initialize PCF85063A failed");
    esp_err_t ret = esp_board_manager_get_device_handle(BOARD_DEVICE_RTC,
                                                        (void **)&s_rtc);
    if (ret != ESP_OK || !s_rtc) {
        esp_board_manager_deinit_device_by_name(BOARD_DEVICE_RTC);
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    return ESP_OK;
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_ERROR(board_rtc_init(), TAG, "RTC init failed");
    ESP_RETURN_ON_FALSE(data && len > 0 && len <= UINT8_MAX, ESP_ERR_INVALID_ARG,
                        TAG, "invalid RTC read");
    return pcf85063a_read_register(&s_rtc->device, reg, data, (uint8_t)len);
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_ERROR(board_rtc_init(), TAG, "RTC init failed");
    ESP_RETURN_ON_FALSE(data && len > 0 && len <= PCF85063_TIME_LEN,
                        ESP_ERR_INVALID_ARG,
                        TAG, "invalid RTC write");
    uint8_t buffer[1 + PCF85063_TIME_LEN] = {reg};
    memcpy(buffer + 1, data, len);
    return pcf85063a_write_register(&s_rtc->device, buffer, (uint8_t)(len + 1));
}

esp_err_t board_rtc_get_time(board_rtc_time_t *time)
{
    ESP_RETURN_ON_FALSE(time, ESP_ERR_INVALID_ARG, TAG, "RTC time output is null");
    ESP_RETURN_ON_ERROR(board_rtc_init(), TAG, "RTC init failed");

    pcf85063a_datetime_t rtc_time = {0};
    ESP_RETURN_ON_ERROR(pcf85063a_get_time_date(&s_rtc->device, &rtc_time),
                        TAG, "read RTC time failed");
    uint8_t second_reg = 0;
    ESP_RETURN_ON_ERROR(pcf85063a_read_register(&s_rtc->device,
                                                PCF85063A_RTC_SECOND_ADDR,
                                                &second_reg, 1),
                        TAG, "read RTC second register failed");
    *time = (board_rtc_time_t) {
        .year = rtc_time.year,
        .month = rtc_time.month,
        .day = rtc_time.day,
        .weekday = rtc_time.dotw,
        .hour = rtc_time.hour,
        .minute = rtc_time.min,
        .second = rtc_time.sec,
        .voltage_low = (second_reg & BIT(7)) != 0,
    };
    return ESP_OK;
}

esp_err_t board_rtc_set_time(const board_rtc_time_t *time)
{
    ESP_RETURN_ON_FALSE(time, ESP_ERR_INVALID_ARG, TAG, "RTC time input is null");
    ESP_RETURN_ON_ERROR(board_rtc_init(), TAG, "RTC init failed");
    const pcf85063a_datetime_t rtc_time = {
        .year = time->year,
        .month = time->month,
        .day = time->day,
        .dotw = time->weekday,
        .hour = time->hour,
        .min = time->minute,
        .sec = time->second,
    };
    return pcf85063a_set_time_date(&s_rtc->device, rtc_time);
}

esp_err_t board_rtc_clear_interrupt_flags(void)
{
    uint8_t ctrl2 = 0;
    ESP_RETURN_ON_ERROR(rtc_read(PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1),
                        TAG, "read RTC Control_2 failed");
    ctrl2 &= (uint8_t)~(PCF85063A_RTC_CTRL_2_AF | PCF85063A_RTC_CTRL_2_TF);
    return rtc_write(PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
}

esp_err_t board_rtc_start_1hz_timer_interrupt(void)
{
    ESP_RETURN_ON_ERROR(board_rtc_init(), TAG, "RTC init failed");
    ESP_RETURN_ON_ERROR(pcf85063a_disable_timer(&s_rtc->device),
                        TAG, "disable RTC timer failed");
    ESP_RETURN_ON_ERROR(board_rtc_clear_interrupt_flags(),
                        TAG, "clear RTC interrupt flags failed");
    const uint8_t timer_mode = PCF85063A_RTC_TIMER_TCF_1HZ |
                               PCF85063A_RTC_TIMER_MODE_TIE |
                               PCF85063A_RTC_TIMER_MODE_TI_TP;
    return pcf85063a_enable_timer(&s_rtc->device, 1, timer_mode);
}

esp_err_t board_rtc_stop_timer_interrupt(void)
{
    ESP_RETURN_ON_ERROR(board_rtc_init(), TAG, "RTC init failed");
    ESP_RETURN_ON_ERROR(pcf85063a_disable_timer(&s_rtc->device),
                        TAG, "disable RTC timer failed");
    return board_rtc_clear_interrupt_flags();
}

uint32_t board_rtc_get_interrupt_count(void)
{
    return s_rtc ? s_rtc->interrupt_count : 0;
}

esp_err_t board_rtc_get_interrupt_status(board_rtc_interrupt_status_t *status)
{
    ESP_RETURN_ON_FALSE(status, ESP_ERR_INVALID_ARG, TAG,
                        "RTC interrupt status output is null");
    uint8_t ctrl2 = 0;
    ESP_RETURN_ON_ERROR(rtc_read(PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1),
                        TAG, "read RTC Control_2 failed");
    *status = (board_rtc_interrupt_status_t) {
        .count = s_rtc->interrupt_count,
        .gpio_level = gpio_get_level(s_rtc->int_gpio),
        .control2 = ctrl2,
    };
    return ESP_OK;
}

esp_err_t board_shared_power_hold_off(void)
{
    (void)gpio_hold_dis(BOARD_SHARED_POWER_GPIO);

    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_SHARED_POWER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gpio_set_level(BOARD_SHARED_POWER_GPIO, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = gpio_hold_en(BOARD_SHARED_POWER_GPIO);
    return ret == ESP_ERR_NOT_SUPPORTED ? ESP_OK : ret;
}


static esp_err_t configure_sleep_input(gpio_num_t gpio_num)
{
    (void)gpio_hold_dis(gpio_num);
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                        "configure sleep input GPIO%d failed", (int)gpio_num);
    esp_err_t ret = gpio_hold_en(gpio_num);
    return ret == ESP_ERR_NOT_SUPPORTED ? ESP_OK : ret;
}

static esp_err_t configure_sleep_output(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_hold_dis(gpio_num);
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                        "configure sleep output GPIO%d failed", (int)gpio_num);
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_num, level), TAG,
                        "set sleep output GPIO%d failed", (int)gpio_num);
    esp_err_t ret = gpio_hold_en(gpio_num);
    return ret == ESP_ERR_NOT_SUPPORTED ? ESP_OK : ret;
}

esp_err_t board_prepare_gpio_deep_sleep(void)
{
    const gpio_num_t input_gpios[] = {
        BOARD_BUTTON_RADIO_GPIO,
        BOARD_I2C_SDA_GPIO,
        BOARD_I2C_SCL_GPIO,
        BOARD_SHARED_SPI_MISO_GPIO,
        BOARD_EPAPER_BUSY_GPIO,
        BOARD_RTC_IRQ_GPIO,
        BOARD_AXP2602_CHARGE_GPIO,
        BOARD_AXP2602_IRQ_GPIO,
        BOARD_SDMMC_CMD_GPIO,
        BOARD_SDMMC_D0_GPIO,
    };
    for (size_t i = 0; i < sizeof(input_gpios) / sizeof(input_gpios[0]); i++) {
        ESP_RETURN_ON_ERROR(configure_sleep_input(input_gpios[i]), TAG,
                            "configure sleep input GPIO%d failed",
                            (int)input_gpios[i]);
    }

    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_EPAPER_POWER_GPIO, 0), TAG,
                        "hold e-paper power off failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_STATUS_LED_GPIO, 0), TAG,
                        "hold WS2812 data low failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_EPAPER_RESET_GPIO, 0), TAG,
                        "hold e-paper reset low failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_EPAPER_DC_GPIO, 0), TAG,
                        "hold e-paper DC low failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_EPAPER_CS_GPIO, 0), TAG,
                        "hold e-paper CS low failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_SHARED_SPI_SCK_GPIO, 0), TAG,
                        "hold shared SPI SCK low failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_SHARED_SPI_MOSI_GPIO, 0), TAG,
                        "hold shared SPI MOSI low failed");
    ESP_RETURN_ON_ERROR(configure_sleep_output(BOARD_SDMMC_CLK_GPIO, 0), TAG,
                        "hold SDMMC CLK low failed");

    gpio_deep_sleep_hold_en();
    ESP_LOGI(TAG, "sleep GPIO levels held; GPIO0 wake path remains unheld");
    return ESP_OK;
}

void board_release_gpio_deep_sleep_holds(void)
{
    gpio_deep_sleep_hold_dis();
    const gpio_num_t held_gpios[] = {
        BOARD_BUTTON_RADIO_GPIO,
        BOARD_I2C_SDA_GPIO,
        BOARD_I2C_SCL_GPIO,
        BOARD_EPAPER_BUSY_GPIO,
        BOARD_RTC_IRQ_GPIO,
        BOARD_AXP2602_CHARGE_GPIO,
        BOARD_AXP2602_IRQ_GPIO,
        BOARD_SDMMC_CMD_GPIO,
        BOARD_SDMMC_D0_GPIO,
        BOARD_EPAPER_POWER_GPIO,
        BOARD_STATUS_LED_GPIO,
        BOARD_EPAPER_RESET_GPIO,
        BOARD_EPAPER_DC_GPIO,
        BOARD_EPAPER_CS_GPIO,
        BOARD_SHARED_SPI_SCK_GPIO,
        BOARD_SHARED_SPI_MOSI_GPIO,
        BOARD_SDMMC_CLK_GPIO,
        BOARD_RADIO_DIO1_GPIO,
        BOARD_RADIO_NSS_GPIO,
        BOARD_RADIO_RESET_GPIO,
        BOARD_RADIO_BUSY_GPIO,
        BOARD_SHARED_SPI_MISO_GPIO,
    };
    for (size_t i = 0; i < sizeof(held_gpios) / sizeof(held_gpios[0]); i++) {
        esp_err_t ret = gpio_hold_dis(held_gpios[i]);
        if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "release GPIO%d hold failed: %s",
                     (int)held_gpios[i], esp_err_to_name(ret));
        }
    }
}
