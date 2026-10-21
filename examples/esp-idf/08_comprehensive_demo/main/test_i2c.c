#include "app_tests.h"

#include <stdio.h>

#include "board_support.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PCF85063A_I2C_ADDRESS 0x51
#define AXP2602_I2C_ADDRESS   0x62
#define I2C_PROBE_TIMEOUT_MS  100
#define I2C_PROBE_RETRIES     2

static const char *TAG = "i2c_test";

static esp_err_t probe_with_retry(i2c_master_bus_handle_t bus,
                                  const i2c_master_bus_config_t *config,
                                  uint8_t address)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt < I2C_PROBE_RETRIES; ++attempt) {
        ret = board_i2c_probe(address, I2C_PROBE_TIMEOUT_MS);
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "probe 0x%02X attempt=%d: %s SDA%d=%d SCL%d=%d",
                 address, attempt + 1, esp_err_to_name(ret),
                 config->sda_io_num, gpio_get_level(config->sda_io_num),
                 config->scl_io_num, gpio_get_level(config->scl_io_num));
        if (ret == ESP_ERR_TIMEOUT && attempt + 1 < I2C_PROBE_RETRIES) {
            const esp_err_t reset_ret = i2c_master_bus_reset(bus);
            ESP_LOGW(TAG, "bus recovery before retry: %s",
                     esp_err_to_name(reset_ret));
            if (reset_ret != ESP_OK) {
                return reset_ret;
            }
        }
    }
    return ret;
}

esp_err_t test_i2c_run(app_test_result_t *result)
{
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t *config = NULL;
    esp_err_t ret = board_i2c_acquire(&bus);
    if (ret != ESP_OK) {
        app_result_set(result, "I2C", APP_TEST_FAIL,
                       "I2C init fail", esp_err_to_name(ret));
        return ret;
    }
    ret = board_i2c_get_config(&config);
    if (ret != ESP_OK || config == NULL) {
        esp_err_t config_error = ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
        app_result_set(result, "I2C", APP_TEST_FAIL,
                       "I2C config fail", esp_err_to_name(config_error));
        return config_error;
    }

    ESP_LOGI(TAG, "before probe: SDA%d=%d SCL%d=%d internal_pullup=%u",
             config->sda_io_num, gpio_get_level(config->sda_io_num),
             config->scl_io_num, gpio_get_level(config->scl_io_num),
             (unsigned)config->flags.enable_internal_pullup);
    esp_err_t rtc_ret = probe_with_retry(bus, config, PCF85063A_I2C_ADDRESS);
    bool rtc = rtc_ret == ESP_OK;
    esp_err_t bat_ret = probe_with_retry(bus, config, AXP2602_I2C_ADDRESS);
    bool bat = bat_ret == ESP_OK;

    char l1[48];
    char l2[48];
    snprintf(l1, sizeof(l1), "I2C RTC:%s BAT:%s", rtc ? "OK" : "--", bat ? "OK" : "--");
    snprintf(l2, sizeof(l2), "SDA%d SCL%d", (int)config->sda_io_num,
             (int)config->scl_io_num);
    const bool pass = rtc && bat;
    app_result_set(result, "I2C",
                   pass ? APP_TEST_PASS : APP_TEST_FAIL, l1, l2);
    return rtc_ret != ESP_OK ? rtc_ret : bat_ret;
}
