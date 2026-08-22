#include <math.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"

static const char *TAG = "ws2812_test";

#define WS2812_DATA_GPIO          21
#define WS2812_LED_COUNT          1
#define WS2812_MAX_BRIGHTNESS     48
#define EPAPER_POWER_GPIO         GPIO_NUM_38
#define EPAPER_POWER_ON_LEVEL     1
#define POWER_STABILIZE_MS        100
#define COLOR_HOLD_MS             1200
#define BREATH_FRAME_MS           20
#define BREATH_STEPS              360
#define RMT_RESOLUTION_HZ         10000000
#define PI_F                      3.14159265358979323846f

static void enable_ws2812_power(void)
{
    const gpio_config_t power_config = {
        .pin_bit_mask = 1ULL << EPAPER_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&power_config));
    ESP_ERROR_CHECK(gpio_set_level(EPAPER_POWER_GPIO, EPAPER_POWER_ON_LEVEL));
    ESP_LOGI(TAG, "Power enabled: gpio_epaper_power=GPIO%d level=%d, settling %d ms",
             EPAPER_POWER_GPIO, EPAPER_POWER_ON_LEVEL, POWER_STABILIZE_MS);
    vTaskDelay(pdMS_TO_TICKS(POWER_STABILIZE_MS));
}

static led_strip_handle_t ws2812_init(void)
{
    const led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_DATA_GPIO,
        .max_leds = WS2812_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    led_strip_handle_t strip = NULL;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    ESP_ERROR_CHECK(led_strip_clear(strip));
    return strip;
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
    enable_ws2812_power();
    led_strip_handle_t strip = ws2812_init();

    ESP_LOGI(TAG, "WS2812 test: GPIO%d, %d LED, GRB wire order, max brightness %d/255",
             WS2812_DATA_GPIO, WS2812_LED_COUNT, WS2812_MAX_BRIGHTNESS);
    show_test_color(strip, "RED", WS2812_MAX_BRIGHTNESS, 0, 0);
    show_test_color(strip, "GREEN", 0, WS2812_MAX_BRIGHTNESS, 0);
    show_test_color(strip, "BLUE", 0, 0, WS2812_MAX_BRIGHTNESS);
    run_color_breathing(strip);
}
