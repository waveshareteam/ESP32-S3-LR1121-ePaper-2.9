#include "app_tests.h"

#include <stdio.h>

#include "board_support.h"

#define PCF85063A_I2C_ADDRESS 0x51
#define AXP2602_I2C_ADDRESS   0x62
#define I2C_PROBE_TIMEOUT_MS  100
#define I2C_PROBE_RETRIES     2

static esp_err_t probe_with_retry(uint8_t address)
{
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt < I2C_PROBE_RETRIES; ++attempt) {
        ret = board_i2c_probe(address, I2C_PROBE_TIMEOUT_MS);
        if (ret == ESP_OK) {
            break;
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

    esp_err_t rtc_ret = probe_with_retry(PCF85063A_I2C_ADDRESS);
    bool rtc = rtc_ret == ESP_OK;
    esp_err_t bat_ret = probe_with_retry(AXP2602_I2C_ADDRESS);
    bool bat = bat_ret == ESP_OK;

    char l1[48];
    char l2[48];
    snprintf(l1, sizeof(l1), "I2C RTC:%s BAT:%s", rtc ? "OK" : "--", bat ? "OK" : "--");
    snprintf(l2, sizeof(l2), "SDA%d SCL%d", (int)config->sda_io_num,
             (int)config->scl_io_num);
    const bool pass = rtc && bat;
    app_result_set(result, "I2C",
                   pass ? APP_TEST_PASS : APP_TEST_FAIL, l1, l2);
    return pass ? ESP_OK : ESP_ERR_NOT_FOUND;
}
