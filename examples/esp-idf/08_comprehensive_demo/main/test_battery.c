#include "app_tests.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

#include "board_support.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "battery_test";

#define BATTERY_TASK_DONE      BIT0
#define BATTERY_POLL_PERIOD_MS 30000U

static TaskHandle_t s_monitor_task;
static EventGroupHandle_t s_monitor_done;
static atomic_bool s_monitor_stop;
static bool s_irq_handler_added;
static uint32_t s_irq_count;

static const char *battery_state_label(board_axp2602_state_t state)
{
    switch (state) {
    case BOARD_AXP2602_STATE_CHARGING:
        return "CHG";
    case BOARD_AXP2602_STATE_DISCHARGING:
        return "DSG";
    case BOARD_AXP2602_STATE_IDLE:
    default:
        return "IDLE";
    }
}

static esp_err_t configure_battery_inputs(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_AXP2602_CHARGE_GPIO) |
                        BIT64(BOARD_AXP2602_IRQ_GPIO),
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
        ret = board_axp2602_init();
    }
    if (ret != ESP_OK) {
        app_result_set(result, "BAT", APP_TEST_FAIL,
                       "AXP2602 init fail", esp_err_to_name(ret));
        return ret;
    }

    board_axp2602_info_t info = {0};
    ret = board_axp2602_get_info(&info);
    if (ret != ESP_OK) {
        app_result_set(result, "BAT", APP_TEST_FAIL,
                       "AXP2602 read fail", esp_err_to_name(ret));
        return ret;
    }

    char line1[48];
    char line2[48];
    snprintf(line1, sizeof(line1), "BAT %umV %+.2fmA",
             (unsigned)info.voltage_mv, (double)info.current_ma);
    snprintf(line2, sizeof(line2), "SOC%u%% %s G47=%d G48=%d",
             (unsigned)info.state_of_charge_percent,
             battery_state_label(info.state),
             gpio_get_level(BOARD_AXP2602_CHARGE_GPIO),
             gpio_get_level(BOARD_AXP2602_IRQ_GPIO));
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
    uint8_t status = 0;
    const esp_err_t ret = board_axp2602_get_irq_status(&status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read AXP2602 IRQ status failed: %s",
                 esp_err_to_name(ret));
        return;
    }
    s_irq_count += notifications;
    ESP_LOGI(TAG,
             "AXP2602 IRQ count=%" PRIu32 " status=0x%02X low_soc=%u soc=%u temp=%u wdt=%u",
             s_irq_count, status,
             (unsigned)((status & BOARD_AXP2602_IRQ_LOW_SOC) != 0),
             (unsigned)((status & BOARD_AXP2602_IRQ_SOC_UPDATE) != 0),
             (unsigned)((status & BOARD_AXP2602_IRQ_OVER_TEMPERATURE) != 0),
             (unsigned)((status & BOARD_AXP2602_IRQ_WATCHDOG_OVERFLOW) != 0));
    if (status != 0) {
        const esp_err_t clear_ret = board_axp2602_clear_irq_status(status);
        if (clear_ret != ESP_OK) {
            ESP_LOGE(TAG, "clear AXP2602 IRQ failed: %s",
                     esp_err_to_name(clear_ret));
        }
    }
}

static void log_battery_sample(void)
{
    board_axp2602_info_t info = {0};
    const esp_err_t ret = board_axp2602_get_info(&info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AXP2602 monitor read failed: %s",
                 esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG,
             "BAT %" PRIu16 "mV %+.2fmA SOC=%u%% temp=%dC state=%s GPIO47=%d GPIO48=%d",
             info.voltage_mv, (double)info.current_ma,
             info.state_of_charge_percent, info.ic_temperature_c,
             battery_state_label(info.state),
             gpio_get_level(BOARD_AXP2602_CHARGE_GPIO),
             gpio_get_level(BOARD_AXP2602_IRQ_GPIO));
}

static void battery_monitor_task(void *arg)
{
    (void)arg;
    while (!atomic_load_explicit(&s_monitor_stop, memory_order_acquire)) {
        const uint32_t notifications = ulTaskNotifyTake(
            pdTRUE, pdMS_TO_TICKS(BATTERY_POLL_PERIOD_MS));
        if (atomic_load_explicit(&s_monitor_stop, memory_order_acquire)) {
            break;
        }
        if (notifications != 0 ||
            gpio_get_level(BOARD_AXP2602_IRQ_GPIO) == 0) {
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
        ret = board_axp2602_init();
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
    atomic_store_explicit(&s_monitor_stop, false, memory_order_release);
    s_irq_count = 0;

    if (xTaskCreate(battery_monitor_task, "battery_monitor", 4096, NULL, 4,
                    &s_monitor_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        goto fail;
    }
    ret = gpio_isr_handler_add(BOARD_AXP2602_IRQ_GPIO,
                               battery_irq_handler, s_monitor_task);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_irq_handler_added = true;
    ret = board_axp2602_configure_irq(0, false);
    if (ret == ESP_OK) {
        ret = board_axp2602_clear_irq_status(BOARD_AXP2602_IRQ_ALL);
    }
    if (ret == ESP_OK) {
        ret = board_axp2602_configure_irq(BOARD_AXP2602_IRQ_ALL, false);
    }
    if (ret == ESP_OK) {
        ret = gpio_set_intr_type(BOARD_AXP2602_IRQ_GPIO, GPIO_INTR_NEGEDGE);
    }
    if (ret == ESP_OK) {
        ret = gpio_intr_enable(BOARD_AXP2602_IRQ_GPIO);
    }
    if (ret != ESP_OK) {
        goto fail;
    }
    if (gpio_get_level(BOARD_AXP2602_IRQ_GPIO) == 0) {
        xTaskNotifyGive(s_monitor_task);
    }
    ESP_LOGI(TAG,
             "AXP2602 monitor active: GPIO47 charge, GPIO48 active-low IRQ");
    return ESP_OK;

fail:
    (void)gpio_intr_disable(BOARD_AXP2602_IRQ_GPIO);
    if (s_irq_handler_added) {
        const esp_err_t remove_ret =
            gpio_isr_handler_remove(BOARD_AXP2602_IRQ_GPIO);
        if (remove_ret != ESP_OK) {
            ESP_LOGE(TAG, "remove AXP2602 ISR after start failure: %s",
                     esp_err_to_name(remove_ret));
            return remove_ret;
        }
        s_irq_handler_added = false;
    }
    atomic_store_explicit(&s_monitor_stop, true, memory_order_release);
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
        first_error = gpio_intr_disable(BOARD_AXP2602_IRQ_GPIO);
        const esp_err_t remove_ret =
            gpio_isr_handler_remove(BOARD_AXP2602_IRQ_GPIO);
        if (remove_ret != ESP_OK) {
            ESP_LOGE(TAG, "remove AXP2602 ISR failed: %s",
                     esp_err_to_name(remove_ret));
            return remove_ret;
        }
        s_irq_handler_added = false;
    }
    atomic_store_explicit(&s_monitor_stop, true, memory_order_release);
    xTaskNotifyGive(s_monitor_task);
    const EventBits_t bits = xEventGroupWaitBits(
        s_monitor_done, BATTERY_TASK_DONE, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(2000));
    if ((bits & BATTERY_TASK_DONE) == 0) {
        ESP_LOGE(TAG, "battery monitor stop timed out");
        return ESP_ERR_TIMEOUT;
    }
    s_monitor_task = NULL;

    esp_err_t ret = board_axp2602_configure_irq(0, false);
    if (first_error == ESP_OK && ret != ESP_OK) {
        first_error = ret;
    }
    ret = board_axp2602_clear_irq_status(BOARD_AXP2602_IRQ_ALL);
    if (first_error == ESP_OK && ret != ESP_OK) {
        first_error = ret;
    }
    return first_error;
}
