#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "dev_led_strip.h"
#include "esp_board_manager.h"
#include "esp_board_periph.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "periph_gpio.h"

static const char *TAG = "ws2812_test";

#define BOARD_STATUS_LED          "status_led"
#define BOARD_POWER_PERIPH        "gpio_epaper_power"
#define WS2812_MAX_BRIGHTNESS     48
#define POWER_STABILIZE_MS        100
#define COLOR_HOLD_MS             1200
#define BREATH_FRAME_MS           20
#define BREATH_STEPS              360
#define PI_F                      3.14159265358979323846f

static void enable_ws2812_power(void)
{
    periph_gpio_handle_t *power = NULL;
    // Acquiring this peripheral applies the board YAML's output/default_level.
    // Keep the reference for the lifetime of the continuous LED demo.
    ESP_ERROR_CHECK(esp_board_periph_ref_handle(BOARD_POWER_PERIPH, (void **)&power));
    ESP_LOGI(TAG, "Power enabled by Board Manager: %s=GPIO%d, settling %d ms",
             BOARD_POWER_PERIPH, (int)power->gpio_num, POWER_STABILIZE_MS);
    vTaskDelay(pdMS_TO_TICKS(POWER_STABILIZE_MS));
}

static led_strip_handle_t ws2812_init(void)
{
    dev_led_strip_handles_t *led = NULL;
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(BOARD_STATUS_LED));
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle(BOARD_STATUS_LED, (void **)&led));
    ESP_ERROR_CHECK(led_strip_clear(led->strip_handle));
    return led->strip_handle;
}

static void show_test_color(led_strip_handle_t strip, const char *name,
                            uint8_t red, uint8_t green, uint8_t blue)
{
    ESP_LOGI(TAG, "Stage %s: R=%u G=%u B=%u", name, red, green, blue);
    ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, red, green, blue));
    ESP_ERROR_CHECK(led_strip_refresh(strip));
    vTaskDelay(pdMS_TO_TICKS(COLOR_HOLD_MS));
}

static void run_color_breathing(led_strip_handle_t strip)
{
    uint16_t hue = 240;
    uint16_t breath_phase = BREATH_STEPS / 2;
    uint16_t frame = 0;
    uint32_t completed_cycles = 0;

    ESP_LOGI(TAG, "Stage BREATH: continuous hue and brightness cycle");
    while (true) {
        const float angle = (2.0f * PI_F * (float)breath_phase) / (float)BREATH_STEPS;
        const float wave = (1.0f - cosf(angle)) * 0.5f;
        const uint8_t brightness = (uint8_t)(wave * WS2812_MAX_BRIGHTNESS + 0.5f);

        ESP_ERROR_CHECK(led_strip_set_pixel_hsv(strip, 0, hue, 255, brightness));
        ESP_ERROR_CHECK(led_strip_refresh(strip));
        vTaskDelay(pdMS_TO_TICKS(BREATH_FRAME_MS));

        hue = (uint16_t)((hue + 1) % 360);
        breath_phase = (uint16_t)((breath_phase + 1) % BREATH_STEPS);
        frame++;
        if (frame == BREATH_STEPS) {
            frame = 0;
            ESP_LOGI(TAG, "Breathing cycle %lu", (unsigned long)++completed_cycles);
        }
    }
}

void app_main(void)
{
    // Prepare the registry, then initialize only the shared power GPIO and LED.
    ESP_ERROR_CHECK(esp_board_manager_prepare());
    enable_ws2812_power();
    led_strip_handle_t strip = ws2812_init();

    ESP_LOGI(TAG, "WS2812 test: Board Manager device %s, max brightness %d/255",
             BOARD_STATUS_LED, WS2812_MAX_BRIGHTNESS);
    show_test_color(strip, "RED", WS2812_MAX_BRIGHTNESS, 0, 0);
    show_test_color(strip, "GREEN", 0, WS2812_MAX_BRIGHTNESS, 0);
    show_test_color(strip, "BLUE", 0, 0, WS2812_MAX_BRIGHTNESS);
    run_color_breathing(strip);
}
