#include <stdio.h>

#include "board_epaper.h"
#include "board_support.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "bat_axp2602";
static TaskHandle_t s_axp2602_irq_task;
static uint32_t s_axp2602_irq_count;

#define AXP2602_INIT_RETRY_ATTEMPTS  (10U)
#define AXP2602_INIT_RETRY_DELAY_MS  (500U)
#define AXP2602_CHARGE_GPIO          GPIO_NUM_47

static esp_err_t configure_charge_detect_gpio(void)
{
    const gpio_config_t charge_gpio_config = {
        .pin_bit_mask = 1ULL << AXP2602_CHARGE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&charge_gpio_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Charge detect GPIO%d ready (high=charging, level=%d)",
                 AXP2602_CHARGE_GPIO,
                 gpio_get_level(AXP2602_CHARGE_GPIO));
    }
    return ret;
}

static esp_err_t initialize_axp2602_with_retry(void)
{
    esp_err_t ret = ESP_FAIL;
    for (unsigned attempt = 1; attempt <= AXP2602_INIT_RETRY_ATTEMPTS; attempt++) {
        ret = board_axp2602_init();
        if (ret == ESP_OK) {
            if (attempt > 1) {
                ESP_LOGI(TAG, "AXP2602 detected on startup attempt %u/%u",
                         attempt, AXP2602_INIT_RETRY_ATTEMPTS);
            }
            return ESP_OK;
        }

        if (attempt < AXP2602_INIT_RETRY_ATTEMPTS) {
            ESP_LOGW(TAG, "AXP2602 not ready: %s; retry %u/%u in %u ms",
                     esp_err_to_name(ret), attempt + 1,
                     AXP2602_INIT_RETRY_ATTEMPTS,
                     AXP2602_INIT_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(AXP2602_INIT_RETRY_DELAY_MS));
        }
    }
    return ret;
}

static void IRAM_ATTR axp2602_irq_handler(void *arg)
{
    BaseType_t should_yield = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)arg, &should_yield);
    if (should_yield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static esp_err_t configure_axp2602_irq(void)
{
    const gpio_config_t irq_gpio_config = {
        .pin_bit_mask = 1ULL << BOARD_AXP2602_IRQ_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&irq_gpio_config);
    if (ret != ESP_OK) {
        return ret;
    }

    s_axp2602_irq_task = xTaskGetCurrentTaskHandle();
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        s_axp2602_irq_task = NULL;
        return ret;
    }
    ret = gpio_isr_handler_add(BOARD_AXP2602_IRQ_GPIO, axp2602_irq_handler,
                               s_axp2602_irq_task);
    if (ret != ESP_OK) {
        s_axp2602_irq_task = NULL;
        return ret;
    }

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
        gpio_intr_disable(BOARD_AXP2602_IRQ_GPIO);
        gpio_isr_handler_remove(BOARD_AXP2602_IRQ_GPIO);
        (void)board_axp2602_configure_irq(0, false);
        s_axp2602_irq_task = NULL;
        return ret;
    }

    if (gpio_get_level(BOARD_AXP2602_IRQ_GPIO) == 0) {
        xTaskNotifyGive(s_axp2602_irq_task);
    }
    ESP_LOGI(TAG, "AXP2602 IRQ enabled on GPIO%d (active-low level, pin=%d)",
             BOARD_AXP2602_IRQ_GPIO, gpio_get_level(BOARD_AXP2602_IRQ_GPIO));
    return ESP_OK;
}

static void try_configure_axp2602_irq(bool *irq_ready)
{
    if (*irq_ready) {
        return;
    }

    esp_err_t ret = configure_axp2602_irq();
    if (ret == ESP_OK) {
        *irq_ready = true;
    } else {
        ESP_LOGW(TAG, "AXP2602 IRQ unavailable; continue polling: %s",
                 esp_err_to_name(ret));
    }
}

static void handle_axp2602_irq(uint32_t notifications)
{
    uint8_t status = 0;
    esp_err_t ret = board_axp2602_get_irq_status(&status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read AXP2602 IRQ status failed: %s", esp_err_to_name(ret));
        return;
    }

    s_axp2602_irq_count += notifications;
    if (status == 0) {
        ESP_LOGW(TAG, "AXP2602 IRQ notification without status, count=%u GPIO%d=%d",
                 (unsigned)s_axp2602_irq_count, BOARD_AXP2602_IRQ_GPIO,
                 gpio_get_level(BOARD_AXP2602_IRQ_GPIO));
        return;
    }

    ESP_LOGI(TAG,
             "AXP2602 IRQ count=%u status=0x%02X: low_soc=%u soc_update=%u "
             "over_temp=%u watchdog=%u",
             (unsigned)s_axp2602_irq_count, status,
             (unsigned)((status & BOARD_AXP2602_IRQ_LOW_SOC) != 0),
             (unsigned)((status & BOARD_AXP2602_IRQ_SOC_UPDATE) != 0),
             (unsigned)((status & BOARD_AXP2602_IRQ_OVER_TEMPERATURE) != 0),
             (unsigned)((status & BOARD_AXP2602_IRQ_WATCHDOG_OVERFLOW) != 0));

    ret = board_axp2602_clear_irq_status(status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "clear AXP2602 IRQ status 0x%02X failed: %s",
                 status, esp_err_to_name(ret));
        return;
    }

    const int level = gpio_get_level(BOARD_AXP2602_IRQ_GPIO);
    ESP_LOGI(TAG, "AXP2602 IRQ status 0x%02X cleared, GPIO%d=%d",
             status, BOARD_AXP2602_IRQ_GPIO, level);
    if (level == 0) {
        ESP_LOGW(TAG, "AXP2602 IRQ remains asserted; status will be checked again");
    }
}

static void wait_for_next_poll(bool irq_ready)
{
    TickType_t remaining =
        pdMS_TO_TICKS(CONFIG_EXAMPLE_AXP2602_POLL_INTERVAL_MS);
    if (!irq_ready) {
        vTaskDelay(remaining);
        return;
    }

    if (gpio_get_level(BOARD_AXP2602_IRQ_GPIO) == 0) {
        xTaskNotifyGive(s_axp2602_irq_task);
    }
    while (remaining > 0) {
        const TickType_t start = xTaskGetTickCount();
        const uint32_t notifications = ulTaskNotifyTake(pdTRUE, remaining);
        if (notifications != 0) {
            handle_axp2602_irq(notifications);
        }

        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (notifications == 0 || elapsed >= remaining) {
            break;
        }
        remaining -= elapsed;
    }
}

static void show_epaper(bool display_ready, const char *title,
                         const char *const *lines, size_t line_count)
{
    if (!display_ready) {
        return;
    }
    esp_err_t ret = board_epaper_show_lines(title, lines, line_count, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "e-paper update failed: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    const esp_err_t charge_gpio_ret = configure_charge_detect_gpio();
    const bool charge_gpio_ready = charge_gpio_ret == ESP_OK;
    if (!charge_gpio_ready) {
        ESP_LOGE(TAG, "Charge detect GPIO%d init failed: %s",
                 AXP2602_CHARGE_GPIO, esp_err_to_name(charge_gpio_ret));
    }

    esp_err_t init_ret = initialize_axp2602_with_retry();
    bool axp_detected = init_ret == ESP_OK;
    if (init_ret == ESP_OK) {
        ESP_LOGI(TAG, "AXP2602 initialized by Board Manager");
    } else {
        ESP_LOGE(TAG, "AXP2602 startup retries exhausted: %s",
                 esp_err_to_name(init_ret));
    }

    bool irq_ready = false;
    if (init_ret == ESP_OK) {
        try_configure_axp2602_irq(&irq_ready);
    }

    esp_err_t display_ret = board_epaper_start(NULL);
    const bool display_ready = display_ret == ESP_OK;
    if (!display_ready) {
        ESP_LOGW(TAG, "e-paper unavailable; continue with serial output: %s",
                 esp_err_to_name(display_ret));
    }

    bool display_has_content = false;

    while (true) {
        board_axp2602_info_t info = {0};
        esp_err_t ret = board_axp2602_get_info(&info);
        if (ret == ESP_OK) {
            if (!axp_detected) {
                axp_detected = true;
                ESP_LOGI(TAG, "AXP2602 detected after startup; enabling IRQ");
            }
            try_configure_axp2602_irq(&irq_ready);

            const int charge_gpio_level =
                charge_gpio_ready ? gpio_get_level(AXP2602_CHARGE_GPIO) : 0;
            const bool is_charging = charge_gpio_level == 1;
            const char *state = is_charging ? "charging" : "not charging";
            const char *charge_state = is_charging ? "CHARGE" : "NOT CHG";
            char l0[40];
            char l1[40];
            char l2[40];
            char l3[40];
            char l4[40];
            char l5[40];
            snprintf(l0, sizeof(l0), "Voltage: %u mV", (unsigned)info.voltage_mv);
            snprintf(l1, sizeof(l1), "Current: %+.2f mA", (double)info.current_ma);
            snprintf(l2, sizeof(l2), "SOC: %u%%", (unsigned)info.state_of_charge_percent);
            snprintf(l3, sizeof(l3), "IC temp: %d C", (int)info.ic_temperature_c);
            snprintf(l4, sizeof(l4), "State: %s, GPIO47=%u", state,
                     (unsigned)charge_gpio_level);
            if (is_charging) {
                snprintf(l5, sizeof(l5), "TTF: %u min", (unsigned)info.time_to_full_min);
            } else if (info.state == BOARD_AXP2602_STATE_DISCHARGING) {
                snprintf(l5, sizeof(l5), "TTE: %u min", (unsigned)info.time_to_empty_min);
            } else {
                snprintf(l5, sizeof(l5), "Estimate: n/a");
            }

            char ui0[32];
            char ui1[32];
            char ui2[32];
            snprintf(ui0, sizeof(ui0), "SOC %u%%  %u.%03uV",
                     (unsigned)info.state_of_charge_percent,
                     (unsigned)(info.voltage_mv / 1000U),
                     (unsigned)(info.voltage_mv % 1000U));
            snprintf(ui1, sizeof(ui1), "%+.1f mA  %s",
                     (double)info.current_ma, charge_state);
            snprintf(ui2, sizeof(ui2), "TEMP %dC  IO47=%u",
                     (int)info.ic_temperature_c,
                     (unsigned)charge_gpio_level);

            const char *ui_lines[] = {ui0, ui1, ui2};
            show_epaper(display_ready, "AXP2602 OK", ui_lines, 3);
            display_has_content = true;
            ESP_LOGI(TAG, "%s, %s, %s, %s, %s, %s, shunt=%u mOhm",
                     l0, l1, l2, l3, l4, l5,
                     (unsigned)info.sense_resistor_mohm);
        } else {
            if (!display_has_content) {
                char err[24];
                snprintf(err, sizeof(err), "%s", esp_err_to_name(ret));
                const char *lines[] = {"NOT FOUND", err, "I2C 0x62"};
                show_epaper(display_ready, "AXP2602", lines, 3);
                display_has_content = true;
            }
            ESP_LOGW(TAG, "Read fail: %s", esp_err_to_name(ret));
        }

        wait_for_next_poll(irq_ready);
    }
}
