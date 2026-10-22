/*****************************************************************************
* | File      	:  	EPD_2in9_V2.c
* | Author      :   Waveshare team
* | Function    :   2.9inch e-paper V2
* | Info        :
*----------------
* |	This version:   V1.2
* | Date        :   2023-12-21
* | Info        :
* -----------------------------------------------------------------------------
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
/* Adapted for ESP-IDF, with checked SPI transfers and bounded BUSY waits.
 * Source: waveshareteam/e-Paper, commit e4bb8924d47c8ac88072cdddea5544e2f4314d92
 * RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_2in9_V2.c
 * The waveform is panel-specific; do not generalize to all SSD16xx panels.
 */
#include "epaper_gray4_v2.h"
#include "../epaper_common.h"
#include "gray4_encode.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd_gray4_v2";
static const uint8_t gray4_lut[159] = {
    0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x60, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x28, 0x60, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2a, 0x60, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x05, 0x14, 0x00, 0x00, 0x1e, 0x1e, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x02, 0x00, 0x05, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x24, 0x22, 0x22, 0x22, 0x23, 0x32, 0x00, 0x00, 0x00, 0x22, 0x17, 0x41,
    0xae, 0x32, 0x28,
};

static esp_err_t transfer(epd_spi_t *spi, int data_mode, const uint8_t *data, size_t size)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(spi->pin_dc, data_mode), TAG, "set DC failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(spi->pin_cs, 0), TAG, "set CS failed");
    spi_transaction_t transaction = {.length = size * 8, .tx_buffer = data};
    esp_err_t ret = spi_device_polling_transmit(spi->spi, &transaction);
    esp_err_t cs_ret = gpio_set_level(spi->pin_cs, 1);
    return ret != ESP_OK ? ret : cs_ret;
}

static esp_err_t command(epd_spi_t *spi, uint8_t cmd, const uint8_t *data, size_t size)
{
    ESP_RETURN_ON_ERROR(transfer(spi, 0, &cmd, 1), TAG, "command 0x%02x failed", cmd);
    return size ? transfer(spi, 1, data, size) : ESP_OK;
}

static esp_err_t wait_ready(epd_spi_t *spi)
{
    const int64_t deadline = esp_timer_get_time() + 15000000;
    while (epd_spi_is_busy(spi)) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "four-gray BUSY timeout (15 s)");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    /* Match the reference driver's settling delay after BUSY releases. */
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t reset_cursor(epd_spi_t *spi)
{
    const uint8_t zero[] = {0, 0};
    ESP_RETURN_ON_ERROR(command(spi, 0x4e, zero, 1), TAG, "X cursor failed");
    return command(spi, 0x4f, zero, 2);
}

static esp_err_t init_gray4(epd_spi_t *spi)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(spi->pin_rst, 1), TAG, "reset high failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(spi->pin_rst, 0), TAG, "reset low failed");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(gpio_set_level(spi->pin_rst, 1), TAG, "reset release failed");
    vTaskDelay(pdMS_TO_TICKS(110));
    ESP_RETURN_ON_ERROR(wait_ready(spi), TAG, "hardware reset failed");
    ESP_RETURN_ON_ERROR(command(spi, 0x12, NULL, 0), TAG, "software reset failed");
    ESP_RETURN_ON_ERROR(wait_ready(spi), TAG, "software reset busy");

    static const struct {
        uint8_t cmd, len, data[4];
    } init[] = {
        {0x74, 1, {0x54}},             /* Analog block */
        {0x7e, 1, {0x3b}},             /* Digital block */
        {0x01, 3, {0x27, 0x01, 0x00}}, /* 296 gates */
        {0x11, 1, {0x03}},             /* X/Y increment */
        {0x44, 2, {0x00, 0x0f}},       /* 128 columns */
        {0x45, 4, {0, 0, 0x27, 0x01}},
        {0x3c, 1, {0x00}},
        {0x21, 2, {0x00, 0x80}},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        ESP_RETURN_ON_ERROR(command(spi, init[i].cmd, init[i].data, init[i].len),
                            TAG, "four-gray init failed");
    }
    ESP_RETURN_ON_ERROR(reset_cursor(spi), TAG, "cursor failed");
    ESP_RETURN_ON_ERROR(wait_ready(spi), TAG, "init busy");
    ESP_RETURN_ON_ERROR(command(spi, 0x32, gray4_lut, 153), TAG, "LUT failed");
    ESP_RETURN_ON_ERROR(wait_ready(spi), TAG, "LUT busy");
    ESP_RETURN_ON_ERROR(command(spi, 0x3f, gray4_lut + 153, 1), TAG, "EOPT failed");
    ESP_RETURN_ON_ERROR(command(spi, 0x03, gray4_lut + 154, 1), TAG, "VGH failed");
    ESP_RETURN_ON_ERROR(command(spi, 0x04, gray4_lut + 155, 3), TAG, "source voltage failed");
    return command(spi, 0x2c, gray4_lut + 158, 1);
}

esp_err_t epd_show_gray4_v2(epd_handle_t panel, const uint8_t *pixels, size_t size)
{
    ESP_RETURN_ON_FALSE(panel && pixels && size == 9472, ESP_ERR_INVALID_ARG,
                        TAG, "expected a 9472-byte native four-gray image");
    const epd_panel_desc_t *desc = epd_get_panel(panel);
    ESP_RETURN_ON_FALSE(desc && desc->ctrl == EPD_CTRL_SSD16XX &&
                        epd_get_width(panel) == 128 && epd_get_height(panel) == 296,
                        ESP_ERR_NOT_SUPPORTED, TAG, "expected SSD16xx 128x296");
    epd_spi_t *spi = epd_get_spi(panel);
    ESP_RETURN_ON_FALSE(spi && spi->spi, ESP_ERR_INVALID_STATE, TAG, "SPI not ready");
    ESP_LOGI(TAG, "selected Waveshare 2.9inch V2 Gray4 waveform (not autodetected)");
    ESP_RETURN_ON_ERROR(init_gray4(spi), TAG, "four-gray initialization failed");

    uint8_t chunk[128];
    for (unsigned plane = 0; plane < 2; ++plane) {
        ESP_RETURN_ON_ERROR(reset_cursor(spi), TAG, "plane cursor failed");
        ESP_RETURN_ON_ERROR(command(spi, plane == 0 ? 0x24 : 0x26, NULL, 0),
                            TAG, "plane command failed");
        for (size_t offset = 0; offset < 4736; offset += sizeof(chunk)) {
            for (size_t i = 0; i < sizeof(chunk); ++i) {
                chunk[i] = gray4_plane_byte(pixels + 2 * (offset + i), plane);
            }
            ESP_RETURN_ON_ERROR(transfer(spi, 1, chunk, sizeof(chunk)), TAG, "plane write failed");
        }
    }
    const uint8_t update = 0xc7;
    ESP_RETURN_ON_ERROR(command(spi, 0x22, &update, 1), TAG, "update control failed");
    ESP_RETURN_ON_ERROR(command(spi, 0x20, NULL, 0), TAG, "update activation failed");
    return wait_ready(spi);
}
