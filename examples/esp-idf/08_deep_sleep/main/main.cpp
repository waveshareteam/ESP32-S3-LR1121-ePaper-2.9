#include <math.h>
#include <stdio.h>

#include <GaugeAXP2602.hpp>

#include "board_epaper.h"
#include "board_radio.h"
#include "board_support.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "deep_sleep";

#define WAKEUP_TIME_SEC 600

static bool init_battery_gauge(GaugeAXP2602 &battery)
{
    i2c_master_bus_handle_t bus = nullptr;
    if (board_i2c_acquire(&bus) != ESP_OK || !battery.begin(bus)) {
        return false;
    }
    battery.setCurrentSenseResistor(GaugeAXP2602::SENSE_RESISTOR_10_MOHM);
    battery.setBatteryDetection(true);
    battery.setCurrentMeasurement(true);
    battery.setThermalDieMeasurement(true);
    battery.setOperatingMode(GaugeAXP2602::OPERATING_MODE_NORMAL);
    return battery.refresh();
}

static void show_wakeup_reason(gpio_num_t boot_gpio)
{
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes == 0 || (causes & BIT(ESP_SLEEP_WAKEUP_UNDEFINED))) {
        ESP_LOGI(TAG, "cold boot or undefined wakeup");
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        ESP_LOGI(TAG, "wakeup from timer");
    }
    if (causes & BIT(ESP_SLEEP_WAKEUP_EXT0)) {
        ESP_LOGI(TAG, "wakeup from BOOT GPIO%d", (int)boot_gpio);
    }
}

static void wait_for_boot_button_release(gpio_num_t boot_gpio)
{
    const TickType_t sample_delay = pdMS_TO_TICKS(10);
    int stable_samples = 0;

    while (stable_samples < 5) {
        if (gpio_get_level(boot_gpio) != 0) {
            stable_samples++;
        } else {
            stable_samples = 0;
        }
        vTaskDelay(sample_delay);
    }
}

extern "C" void app_main(void)
{
    gpio_deep_sleep_hold_dis();
    ESP_ERROR_CHECK(board_support_prepare());
    // Release pin holds from the previous wake, then initialize LR1121 so the
    // later board_radio_sleep() call sends a real SET_SLEEP command.
    ESP_ERROR_CHECK(board_radio_init(NULL, NULL));
    const gpio_num_t boot_gpio = board_boot_gpio();
    ESP_ERROR_CHECK(boot_gpio == GPIO_NUM_NC ? ESP_ERR_NOT_FOUND : ESP_OK);
    show_wakeup_reason(boot_gpio);

    GaugeAXP2602 battery;
    const bool battery_ready = init_battery_gauge(battery);
    if (!battery_ready) {
        ESP_LOGW(TAG, "SensorLib AXP2602 init failed; continue without battery gauge");
    }

    char l0[40];
    char l1[40];
    const char *axp_mode = battery_ready ? "SLEEP" : "NOT FOUND";
    snprintf(l0, sizeof(l0), "TIMER %ds  |  KEY GPIO%d", WAKEUP_TIME_SEC, (int)boot_gpio);
    snprintf(l1, sizeof(l1), "LR1121 OFF  |  AXP %s", axp_mode);
    const char *lines[] = {
        "DEEP SLEEP",
        l0,
        l1,
    };
    ESP_ERROR_CHECK(board_epaper_show_lines("SLEEP", lines, 3, true));
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)WAKEUP_TIME_SEC * 1000000ULL));

    gpio_config_t boot = {
        .pin_bit_mask = BIT64(boot_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot));
    wait_for_boot_button_release(boot_gpio);

    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(boot_gpio, 0));
    ESP_ERROR_CHECK(rtc_gpio_init(boot_gpio));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(boot_gpio));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(boot_gpio));

    ESP_ERROR_CHECK(board_radio_sleep());
    ESP_LOGI(TAG, "LR1121 Sleep accepted before power off");
    if (battery_ready) {
        battery.sleep();
        ESP_ERROR_CHECK(battery.isSleepModeEnabled() ? ESP_OK : ESP_FAIL);
        ESP_LOGI(TAG, "AXP2602 entered sleep mode");
    }
    ESP_ERROR_CHECK(board_epaper_sleep());

    // A/B test: the AXP2602 is sleeping before ESP deep sleep.
    // The LR1121 supply is removed during deep sleep. Isolate every connected
    // GPIO so the ESP32 cannot back-power the unpowered radio through its I/O.
    ESP_ERROR_CHECK(board_radio_isolate_powered_off_pins());
    ESP_ERROR_CHECK(board_shared_power_hold_off());

    gpio_deep_sleep_hold_en();
    ESP_LOGI(TAG, "start deep sleep");
    esp_deep_sleep_start();
}
