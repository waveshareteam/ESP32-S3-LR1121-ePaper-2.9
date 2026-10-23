#include "app_tests.h"

#include <stdatomic.h>

#include "app_config.h"
#include "board_support.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "buttons";

#define BUTTON_SAMPLE_MS 10
#define BUTTON_TASK_DONE BIT0

typedef struct {
    gpio_num_t gpio;
    bool raw_pressed;
    bool stable_pressed;
    TickType_t raw_changed_at;
} button_filter_t;

static QueueHandle_t s_events;
static EventGroupHandle_t s_done;
static TaskHandle_t s_task;
static atomic_bool s_stop;
static bool s_gpio0_isr_added;
static bool s_gpio1_isr_added;

static void IRAM_ATTR button_isr(void *arg)
{
    (void)arg;
    TaskHandle_t task = s_task;
    if (!task) {
        return;
    }
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void post_button_event(app_runtime_event_type_t type)
{
    app_runtime_event_t event = {
        .type = type,
        .result = ESP_OK,
    };
    if (!s_events ||
        xQueueSend(s_events, &event, pdMS_TO_TICKS(50)) != pdTRUE) {
        ESP_LOGE(TAG, "runtime event queue full; button event %d dropped",
                 (int)type);
    }
}

static bool filter_sample(button_filter_t *filter, TickType_t now,
                          bool *pressed)
{
    const bool raw_pressed = gpio_get_level(filter->gpio) == 0;
    if (raw_pressed != filter->raw_pressed) {
        filter->raw_pressed = raw_pressed;
        filter->raw_changed_at = now;
        return false;
    }
    if (raw_pressed == filter->stable_pressed ||
        (now - filter->raw_changed_at) <
            pdMS_TO_TICKS(TEST_BUTTON_DEBOUNCE_MS)) {
        return false;
    }

    filter->stable_pressed = raw_pressed;
    *pressed = raw_pressed;
    return true;
}

static void button_task(void *arg)
{
    (void)arg;
    TickType_t now = xTaskGetTickCount();
    button_filter_t gpio0 = {
        .gpio = BOARD_BUTTON_SLEEP_GPIO,
        .raw_pressed = gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) == 0,
        .stable_pressed = gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) == 0,
        .raw_changed_at = now,
    };
    button_filter_t gpio1 = {
        .gpio = BOARD_BUTTON_RADIO_GPIO,
        .raw_pressed = gpio_get_level(BOARD_BUTTON_RADIO_GPIO) == 0,
        .stable_pressed = gpio_get_level(BOARD_BUTTON_RADIO_GPIO) == 0,
        .raw_changed_at = now,
    };
    bool gpio0_click_pending = false;
    TickType_t gpio0_first_release = 0;

    while (!atomic_load_explicit(&s_stop, memory_order_acquire)) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BUTTON_SAMPLE_MS));
        now = xTaskGetTickCount();

        bool pressed = false;
        if (filter_sample(&gpio1, now, &pressed) && pressed) {
            post_button_event(APP_EVENT_BUTTON_GPIO1);
        }

        if (filter_sample(&gpio0, now, &pressed) && !pressed) {
            const TickType_t click_window =
                pdMS_TO_TICKS(TEST_GPIO0_DOUBLE_CLICK_MS);
            if (gpio0_click_pending &&
                (now - gpio0_first_release) <= click_window) {
                gpio0_click_pending = false;
                ESP_LOGI(TAG, "GPIO0 double click accepted");
                post_button_event(APP_EVENT_BUTTON_GPIO0);
            } else {
                gpio0_click_pending = true;
                gpio0_first_release = now;
                ESP_LOGI(TAG, "GPIO0 first click; double-click window opened");
            }
        }

        if (gpio0_click_pending &&
            (now - gpio0_first_release) >
                pdMS_TO_TICKS(TEST_GPIO0_DOUBLE_CLICK_MS)) {
            gpio0_click_pending = false;
            ESP_LOGI(TAG, "GPIO0 single click expired; staying active");
        }
    }

    xEventGroupSetBits(s_done, BUTTON_TASK_DONE);
    vTaskDelete(NULL);
}

static void remove_isr_handlers(void)
{
    (void)gpio_intr_disable(BOARD_BUTTON_SLEEP_GPIO);
    (void)gpio_intr_disable(BOARD_BUTTON_RADIO_GPIO);
    if (s_gpio0_isr_added) {
        (void)gpio_isr_handler_remove(BOARD_BUTTON_SLEEP_GPIO);
        s_gpio0_isr_added = false;
    }
    if (s_gpio1_isr_added) {
        (void)gpio_isr_handler_remove(BOARD_BUTTON_RADIO_GPIO);
        s_gpio1_isr_added = false;
    }
}

esp_err_t app_buttons_start(QueueHandle_t events)
{
    ESP_RETURN_ON_FALSE(events, ESP_ERR_INVALID_ARG, TAG,
                        "event queue is null");
    if (s_task) {
        return ESP_OK;
    }

    if (!s_done) {
        s_done = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_done, ESP_ERR_NO_MEM, TAG,
                            "create button event group failed");
    }
    xEventGroupClearBits(s_done, BUTTON_TASK_DONE);
    s_events = events;
    atomic_store_explicit(&s_stop, false, memory_order_release);

    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_BUTTON_SLEEP_GPIO) |
                        BIT64(BOARD_BUTTON_RADIO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        goto fail;
    }

    /* board_support_prepare() owns the shared GPIO ISR service. */
    ret = gpio_isr_handler_add(BOARD_BUTTON_SLEEP_GPIO, button_isr, NULL);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_gpio0_isr_added = true;
    ret = gpio_isr_handler_add(BOARD_BUTTON_RADIO_GPIO, button_isr, NULL);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_gpio1_isr_added = true;

    if (xTaskCreate(button_task, "button_task", 3584, NULL, 8, &s_task) !=
        pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    ESP_LOGI(TAG,
             "active-low GPIO0/GPIO1 ready; GPIO0 is a boot strap and is not driven");
    return ESP_OK;

fail:
    remove_isr_handlers();
    s_events = NULL;
    return ret;
}

esp_err_t app_buttons_stop(void)
{
    TaskHandle_t task = s_task;
    if (!task) {
        return ESP_OK;
    }

    /* Prevent either ISR from observing s_task after the task exits. */
    (void)gpio_intr_disable(BOARD_BUTTON_SLEEP_GPIO);
    (void)gpio_intr_disable(BOARD_BUTTON_RADIO_GPIO);
    atomic_store_explicit(&s_stop, true, memory_order_release);
    xTaskNotifyGive(task);
    EventBits_t bits = xEventGroupWaitBits(
        s_done, BUTTON_TASK_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(1500));
    if ((bits & BUTTON_TASK_DONE) == 0) {
        ESP_LOGE(TAG, "button task stop timed out; resources retained");
        return ESP_ERR_TIMEOUT;
    }

    remove_isr_handlers();
    s_task = NULL;
    s_events = NULL;
    return ESP_OK;
}

esp_err_t app_buttons_wait_released(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = BIT64(BOARD_BUTTON_SLEEP_GPIO) |
                        BIT64(BOARD_BUTTON_RADIO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                        "configure button release inputs failed");

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
    TickType_t released_since = 0;
    while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
        const bool released =
            gpio_get_level(BOARD_BUTTON_SLEEP_GPIO) != 0 &&
            gpio_get_level(BOARD_BUTTON_RADIO_GPIO) != 0;
        if (!released) {
            released_since = 0;
        } else if (released_since == 0) {
            released_since = xTaskGetTickCount();
        } else if ((xTaskGetTickCount() - released_since) >=
                   pdMS_TO_TICKS(TEST_BUTTON_RELEASE_MS)) {
            ESP_LOGI(TAG, "buttons stably released");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_SAMPLE_MS));
    }

    ESP_LOGE(TAG, "button remains held; refusing deep sleep");
    return ESP_ERR_TIMEOUT;
}
