#include <stdbool.h>
#include <stdint.h>

#include "board_support.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c_scan";

#define PCF85063A_I2C_ADDRESS 0x51
#define AXP2602_I2C_ADDRESS   0x62
#define I2C_PROBE_TIMEOUT_MS  100

static const char *known_device_name(uint8_t addr)
{
    switch (addr) {
    case PCF85063A_I2C_ADDRESS:
        return "PCF85063A RTC";
    case AXP2602_I2C_ADDRESS:
        return "AXP2602 fuel gauge";
    default:
        return "";
    }
}

static bool i2c_bus_is_idle(const i2c_master_bus_config_t *config)
{
    int sda = gpio_get_level(config->sda_io_num);
    int scl = gpio_get_level(config->scl_io_num);
    ESP_LOGI(TAG, "bus idle level: SDA=%d SCL=%d", sda, scl);
    return sda != 0 && scl != 0;
}

void app_main(void)
{
    const i2c_master_bus_config_t *i2c_config = NULL;
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(board_support_prepare());
    ESP_ERROR_CHECK(board_epaper_power_set(true));
    ESP_ERROR_CHECK(board_i2c_get_config(&i2c_config));
    ESP_ERROR_CHECK(board_i2c_acquire(&i2c_bus));
    ESP_LOGI(TAG, "I2C scan: bus=%p SDA=GPIO%d SCL=GPIO%d", (void *)i2c_bus,
             (int)i2c_config->sda_io_num, (int)i2c_config->scl_io_num);

    while (true) {
        if (!i2c_bus_is_idle(i2c_config)) {
            ESP_LOGW(TAG, "I2C bus is held low; check power, pull-ups, and shared pins");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        int found = 0;
        for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
            esp_err_t ret = board_i2c_probe(addr, I2C_PROBE_TIMEOUT_MS);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "found 0x%02x %s", (unsigned int)addr, known_device_name(addr));
                found++;
            } else if (ret != ESP_ERR_NOT_FOUND && ret != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "probe 0x%02x failed: %s", (unsigned int)addr, esp_err_to_name(ret));
            }
        }
        ESP_LOGI(TAG, "scan done, found %d device(s)", found);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
