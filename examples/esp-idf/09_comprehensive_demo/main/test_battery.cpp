#include "app_tests.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include <GaugeAXP2602.hpp>
#include <atomic>
#include <memory>
#include <new>
#include <utility>

#include "board_support.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "battery_test";

#define BATTERY_TASK_DONE      BIT0
#define BATTERY_POLL_PERIOD_MS 30000U
#define AXP2602_CHARGE_GPIO    GPIO_NUM_47
#define AXP2602_IRQ_GPIO       GPIO_NUM_48
#define AXP2602_IRQ_LOW_SOC    (1U << 0)
#define AXP2602_IRQ_SOC_UPDATE (1U << 1)
#define AXP2602_IRQ_OVER_TEMP  (1U << 2)
#define AXP2602_IRQ_WATCHDOG   (1U << 3)

static TaskHandle_t s_monitor_task;
static EventGroupHandle_t s_monitor_done;
static std::atomic_bool s_monitor_stop{false};
static bool s_irq_handler_added;
static uint32_t s_irq_count;
static std::unique_ptr<GaugeAXP2602> s_battery;

static const char *battery_state_label(float current_ma)
{
    if (current_ma > 25.0f) {
        return "CHG";
    }
    if (current_ma < -25.0f) {
        return "DSG";
    }
    return "IDLE";
}

static esp_err_t init_battery_gauge(void)
{
    if (s_battery) {
        if (s_battery->isSleepModeEnabled()) {
            s_battery->wakeup();
            if (s_battery->isSleepModeEnabled()) {
                return ESP_FAIL;
            }
        }
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t ret = board_i2c_acquire(&bus);
    if (ret != ESP_OK) {
        return ret;
    }

    std::unique_ptr<GaugeAXP2602> battery(
        new (std::nothrow) GaugeAXP2602());
    if (!battery) {
        return ESP_ERR_NO_MEM;
    }
    if (!battery->begin(bus)) {
        return ESP_ERR_NOT_FOUND;
    }
    battery->setCurrentSenseResistor(GaugeAXP2602::SENSE_RESISTOR_10_MOHM);
    battery->setBatteryDetection(true);
    battery->setCurrentMeasurement(true);
    battery->setThermalDieMeasurement(true);
    battery->setOperatingMode(GaugeAXP2602::OPERATING_MODE_NORMAL);
    if (!battery->refresh()) {
        return ESP_FAIL;
    }
    s_battery = std::move(battery);
    return ESP_OK;
}

static esp_err_t configure_battery_inputs(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(AXP2602_CHARGE_GPIO) |
                        BIT64(AXP2602_IRQ_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

esp_err_t test_battery_run(app_test_result_t *result)
{
    esp_err_t ret = configure_battery_inputs();
    if (ret == ESP_OK) {
        ret = init_battery_gauge();
    }
    if (ret != ESP_OK) {
        app_result_set(result, "BAT", APP_TEST_FAIL,
                       "AXP2602 init fail", esp_err_to_name(ret));
        return ret;
    }

    if (!s_battery->refresh()) {
        app_result_set(result, "BAT", APP_TEST_FAIL,
                       "AXP2602 read fail", "SensorLib refresh");
        return ESP_FAIL;
    }
    const uint16_t voltage_mv = s_battery->getVoltage();
    const float current_ma = s_battery->getCurrent();
    const uint8_t state_of_charge = s_battery->getStateOfCharge();

    char line1[48];
    char line2[48];
    snprintf(line1, sizeof(line1), "BAT %umV %+.2fmA",
             (unsigned)voltage_mv, (double)current_ma);
    snprintf(line2, sizeof(line2), "SOC%u%% %s G47=%d G48=%d",
             (unsigned)state_of_charge,
             battery_state_label(current_ma),
             gpio_get_level(AXP2602_CHARGE_GPIO),
             gpio_get_level(AXP2602_IRQ_GPIO));
    app_result_set(result, "BAT", APP_TEST_PASS, line1, line2);
    return ESP_OK;
}

static void IRAM_ATTR battery_irq_handler(void *arg)
{
    BaseType_t should_yield = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)arg, &should_yield);
    if (should_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void handle_battery_irq(uint32_t notifications)
{
    if (!s_battery || !s_battery->refresh()) {
        ESP_LOGE(TAG, "read AXP2602 IRQ status failed");
        return;
    }
    const uint8_t status = static_cast<uint8_t>(s_battery->getIRQStatus());
    s_irq_count += notifications;
    ESP_LOGI(TAG,
             "AXP2602 IRQ count=%" PRIu32 " status=0x%02X low_soc=%u soc=%u temp=%u wdt=%u",
             s_irq_count, status,
             (unsigned)((status & AXP2602_IRQ_LOW_SOC) != 0),
             (unsigned)((status & AXP2602_IRQ_SOC_UPDATE) != 0),
             (unsigned)((status & AXP2602_IRQ_OVER_TEMP) != 0),
             (unsigned)((status & AXP2602_IRQ_WATCHDOG) != 0));
    if (status != 0) {
        s_battery->clearIRQStatus();
        if (!s_battery->refresh()) {
            ESP_LOGE(TAG, "clear AXP2602 IRQ failed");
        }
    }
}

static void log_battery_sample(void)
{
    if (!s_battery || !s_battery->refresh()) {
        ESP_LOGW(TAG, "AXP2602 monitor refresh failed");
        return;
    }
    const uint16_t voltage_mv = s_battery->getVoltage();
    const float current_ma = s_battery->getCurrent();
    const uint8_t state_of_charge = s_battery->getStateOfCharge();
    const float temperature_c = s_battery->getThermalDieTemperature();
    ESP_LOGI(TAG,
             "BAT %" PRIu16 "mV %+.2fmA SOC=%u%% temp=%.1fC state=%s GPIO47=%d GPIO48=%d",
             voltage_mv, (double)current_ma, state_of_charge,
             (double)temperature_c, battery_state_label(current_ma),
             gpio_get_level(AXP2602_CHARGE_GPIO),
             gpio_get_level(AXP2602_IRQ_GPIO));
}

static void battery_monitor_task(void *arg)
{
    (void)arg;
    while (!s_monitor_stop.load(std::memory_order_acquire)) {
        const uint32_t notifications = ulTaskNotifyTake(
            pdTRUE, pdMS_TO_TICKS(BATTERY_POLL_PERIOD_MS));
        if (s_monitor_stop.load(std::memory_order_acquire)) {
            break;
        }
        if (notifications != 0 ||
            gpio_get_level(AXP2602_IRQ_GPIO) == 0) {
            handle_battery_irq(notifications == 0 ? 1 : notifications);
        }
        log_battery_sample();
    }
    xEventGroupSetBits(s_monitor_done, BATTERY_TASK_DONE);
    vTaskDelete(NULL);
}

esp_err_t app_battery_monitor_start(void)
{
    if (s_monitor_task) {
        return ESP_OK;
    }
    esp_err_t ret = configure_battery_inputs();
    if (ret == ESP_OK) {
        ret = init_battery_gauge();
    }
    if (ret != ESP_OK) {
        return ret;
    }
    if (!s_monitor_done) {
        s_monitor_done = xEventGroupCreate();
        if (!s_monitor_done) {
            return ESP_ERR_NO_MEM;
        }
    }
    xEventGroupClearBits(s_monitor_done, BATTERY_TASK_DONE);
    s_monitor_stop.store(false, std::memory_order_release);
    s_irq_count = 0;

    if (xTaskCreate(battery_monitor_task, "battery_monitor", 4096, NULL, 4,
                    &s_monitor_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        goto fail;
    }
    ret = gpio_isr_handler_add(AXP2602_IRQ_GPIO,
                               battery_irq_handler, s_monitor_task);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_irq_handler_added = true;
    s_battery->disableAllIRQ();
    s_battery->clearIRQStatus();
    s_battery->enableIRQ(GaugeAXP2602::IRQ_ENABLE_LOW_BATTERY, true, true);
    s_battery->enableIRQ(GaugeAXP2602::IRQ_ENABLE_SOC_UPDATE, true, true);
    s_battery->enableIRQ(GaugeAXP2602::IRQ_ENABLE_OVER_TEMPERATURE, true, true);
    s_battery->enableIRQ(GaugeAXP2602::IRQ_ENABLE_WDT_TIMEOUT, true, true);
    ret = s_battery->refresh() ? ESP_OK : ESP_FAIL;
    if (ret == ESP_OK) {
        ret = gpio_set_intr_type(AXP2602_IRQ_GPIO, GPIO_INTR_NEGEDGE);
    }
    if (ret == ESP_OK) {
        ret = gpio_intr_enable(AXP2602_IRQ_GPIO);
    }
    if (ret != ESP_OK) {
        goto fail;
    }
    if (gpio_get_level(AXP2602_IRQ_GPIO) == 0) {
        xTaskNotifyGive(s_monitor_task);
    }
    ESP_LOGI(TAG,
             "AXP2602 monitor active: GPIO47 charge, GPIO48 active-low IRQ");
    return ESP_OK;

fail:
    (void)gpio_intr_disable(AXP2602_IRQ_GPIO);
    if (s_irq_handler_added) {
        const esp_err_t remove_ret =
            gpio_isr_handler_remove(AXP2602_IRQ_GPIO);
        if (remove_ret != ESP_OK) {
            ESP_LOGE(TAG, "remove AXP2602 ISR after start failure: %s",
                     esp_err_to_name(remove_ret));
            return remove_ret;
        }
        s_irq_handler_added = false;
    }
    s_monitor_stop.store(true, std::memory_order_release);
    xTaskNotifyGive(s_monitor_task);
    const EventBits_t bits = xEventGroupWaitBits(
        s_monitor_done, BATTERY_TASK_DONE, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(1500));
    if ((bits & BATTERY_TASK_DONE) == 0) {
        ESP_LOGE(TAG, "battery monitor cleanup timed out after start failure");
        return ESP_ERR_TIMEOUT;
    }
    s_monitor_task = NULL;
    return ret;
}

esp_err_t app_battery_monitor_stop(void)
{
    if (!s_monitor_task) {
        return ESP_OK;
    }
    esp_err_t first_error = ESP_OK;
    if (s_irq_handler_added) {
        first_error = gpio_intr_disable(AXP2602_IRQ_GPIO);
        const esp_err_t remove_ret =
            gpio_isr_handler_remove(AXP2602_IRQ_GPIO);
        if (remove_ret != ESP_OK) {
            ESP_LOGE(TAG, "remove AXP2602 ISR failed: %s",
                     esp_err_to_name(remove_ret));
            return remove_ret;
        }
        s_irq_handler_added = false;
    }
    s_monitor_stop.store(true, std::memory_order_release);
    xTaskNotifyGive(s_monitor_task);
    const EventBits_t bits = xEventGroupWaitBits(
        s_monitor_done, BATTERY_TASK_DONE, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(2000));
    if ((bits & BATTERY_TASK_DONE) == 0) {
        ESP_LOGE(TAG, "battery monitor stop timed out");
        return ESP_ERR_TIMEOUT;
    }
    s_monitor_task = NULL;

    if (s_battery) {
        s_battery->disableAllIRQ();
        s_battery->clearIRQStatus();
        if (!s_battery->refresh() && first_error == ESP_OK) {
            first_error = ESP_FAIL;
        }
    }
    return first_error;
}

esp_err_t app_battery_prepare_sleep(void)
{
    if (s_monitor_task) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = init_battery_gauge();
    if (ret != ESP_OK) {
        s_battery.reset();
        return ret;
    }

    s_battery->disableAllIRQ();
    s_battery->clearIRQStatus();
    ret = s_battery->refresh() ? ESP_OK : ESP_FAIL;
    if (ret == ESP_OK) {
        s_battery->sleep();
        ret = s_battery->isSleepModeEnabled() ? ESP_OK : ESP_FAIL;
    }

    // Destroy SensorLib's I2C device before Board Manager releases the bus.
    s_battery.reset();
    return ret;
}
