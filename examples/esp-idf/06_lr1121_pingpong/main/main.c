#include <stdio.h>
#include <string.h>

#include "board_epaper.h"
#include "board_radio.h"
#include "board_support.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lr1121_pingpong";

#define LISTEN_WINDOW_MS           500
#define REPLY_WINDOW_MS            2000
#define PING_INTERVAL_MIN_MS       3000
#define PING_INTERVAL_JITTER_MS    3000
#define DISPLAY_REFRESH_INTERVAL_MS 30000
#define DISPLAY_LINE_CHARS         34U

static TickType_t s_last_display_tick;
static bool s_displayed_once;

static const char *radio_state_short_name(const char *name)
{
    if (strcmp(name, "chip not found") == 0) {
        return "CHIP MISS";
    }
    if (strcmp(name, "rx timeout") == 0) {
        return "RX TIMEOUT";
    }
    if (strcmp(name, "tx timeout") == 0) {
        return "TX TIMEOUT";
    }
    if (strcmp(name, "crc mismatch") == 0) {
        return "BAD CRC";
    }
    if (strcmp(name, "spi cmd failed") == 0) {
        return "SPI FAIL";
    }
    if (strcmp(name, "invalid freq") == 0) {
        return "BAD FREQ";
    }
    if (strcmp(name, "invalid bw") == 0) {
        return "BAD BW";
    }
    if (strcmp(name, "invalid sf") == 0) {
        return "BAD SF";
    }
    if (strcmp(name, "invalid power") == 0) {
        return "BAD PWR";
    }
    return strcmp(name, "OK") == 0 ? "OK" : "ERROR";
}

static void format_payload_for_display(const uint8_t *payload, size_t payload_len,
                                       char *output, size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t written = 0;

    if (!output || output_size == 0) {
        return;
    }
    const size_t capacity =
        output_size - 1U < DISPLAY_LINE_CHARS
            ? output_size - 1U
            : DISPLAY_LINE_CHARS;
    if (!payload || payload_len == 0U) {
        snprintf(output, output_size, "PAYLOAD: -");
        return;
    }

    const char prefix[] = "PAYLOAD: ";
    for (size_t i = 0; i < sizeof(prefix) - 1U && written < capacity; ++i) {
        output[written++] = prefix[i];
    }
    size_t encoded_length = 0;
    for (size_t input = 0; input < payload_len; ++input) {
        const uint8_t ch = payload[input];
        encoded_length += ch >= 0x20 && ch <= 0x7E ? 1U : 4U;
    }

    const size_t available = capacity - written;
    const bool truncated = encoded_length > available;
    const size_t marker_size = truncated && available > 0U ? 1U : 0U;
    const size_t payload_capacity = available - marker_size;
    size_t payload_written = 0;
    for (size_t input = 0; input < payload_len; ++input) {
        const uint8_t ch = payload[input];
        const size_t encoded_size = ch >= 0x20 && ch <= 0x7E ? 1U : 4U;
        if (encoded_size > payload_capacity - payload_written) {
            break;
        }
        if (ch >= 0x20 && ch <= 0x7E) {
            output[written++] = (char)ch;
        } else {
            output[written++] = '\\';
            output[written++] = 'x';
            output[written++] = hex[ch >> 4];
            output[written++] = hex[ch & 0x0F];
        }
        payload_written += encoded_size;
    }
    if (marker_size != 0U) {
        output[written++] = '>';
    }
    output[written] = '\0';
}

static void show_radio_status(const board_radio_status_t *status, const char *phase, bool force)
{
    TickType_t now = xTaskGetTickCount();
    if (!force && s_displayed_once &&
        (now - s_last_display_tick) < pdMS_TO_TICKS(DISPLAY_REFRESH_INTERVAL_MS)) {
        return;
    }

    char l0[DISPLAY_LINE_CHARS + 1U];
    char l1[DISPLAY_LINE_CHARS + 1U];
    char l2[DISPLAY_LINE_CHARS + 1U];
    char l3[DISPLAY_LINE_CHARS + 1U];
    char l4[DISPLAY_LINE_CHARS + 1U];
    char payload[DISPLAY_LINE_CHARS + 1U];
    snprintf(l0, sizeof(l0), "P:%s S:%d %s", phase, status->state,
             radio_state_short_name(board_radio_state_name(status->state)));
    snprintf(l1, sizeof(l1), "RF %.1fMHz BW%.0f", (double)status->config.frequency_mhz,
             (double)status->config.bandwidth_khz);
    snprintf(l2, sizeof(l2), "SF%u CR%u PWR%ddBm", status->config.spreading_factor,
             status->config.coding_rate, status->config.output_power_dbm);
    snprintf(l3, sizeof(l3), "TX %lu | RX %lu", (unsigned long)status->tx_count,
             (unsigned long)status->rx_count);
    snprintf(l4, sizeof(l4), "RSSI %.1f | SNR %.1f", (double)status->last_rssi,
             (double)status->last_snr);
    format_payload_for_display((const uint8_t *)status->last_payload,
                               status->last_payload_len,
                               payload, sizeof(payload));
    const char *lines[] = {l0, l1, l2, l3, l4, payload};
    esp_err_t ret = board_epaper_show_lines("LR1121", lines, 6, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "e-paper status update failed: %s", esp_err_to_name(ret));
        return;
    }
    s_last_display_tick = now;
    s_displayed_once = true;
}

static TickType_t schedule_next_ping(void)
{
    uint32_t delay_ms = PING_INTERVAL_MIN_MS + (esp_random() % PING_INTERVAL_JITTER_MS);
    return xTaskGetTickCount() + pdMS_TO_TICKS(delay_ms);
}

static bool ping_is_due(TickType_t deadline)
{
    return (int32_t)(xTaskGetTickCount() - deadline) >= 0;
}

static const char *handle_received_packet(board_radio_status_t *status)
{
    char received[sizeof(status->last_payload)];
    const size_t received_len =
        status->last_payload_len < sizeof(received) - 1U
            ? status->last_payload_len
            : sizeof(received) - 1U;
    const bool is_c_string =
        memchr(status->last_payload, '\0', received_len) == NULL;
    memcpy(received, status->last_payload, received_len);
    received[received_len] = '\0';
    ESP_LOGI(TAG, "rx payload_len=%u rssi=%.1f snr=%.1f",
             (unsigned)received_len,
             (double)status->last_rssi, (double)status->last_snr);

    if (!is_c_string || received_len < 5U ||
        memcmp(received, "PING ", 5U) != 0) {
        return is_c_string && received_len >= 5U &&
                       memcmp(received, "PONG ", 5U) == 0
                   ? "rx pong"
                   : "rx packet";
    }

    char reply[sizeof(status->last_payload)];
    memcpy(reply, "PONG ", 5U);
    memcpy(reply + 5U, received + 5U, received_len - 5U);
    reply[received_len] = '\0';
    esp_err_t ret = board_radio_send(status, reply);
    ESP_LOGI(TAG, "tx reply=%s state=%d", reply, status->state);
    return ret == ESP_OK ? "tx pong" : "pong fail";
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_support_prepare());
    ESP_ERROR_CHECK(board_epaper_start(NULL));

    board_radio_config_t cfg = BOARD_RADIO_CONFIG_DEFAULT();
    board_radio_status_t status = {0};
    esp_err_t ret = board_radio_init(&cfg, &status);
    show_radio_status(&status, ret == ESP_OK ? "ready" : "init fail", true);

    TickType_t next_ping = schedule_next_ping();
    while (true) {
        ret = board_radio_receive(&status, LISTEN_WINDOW_MS);
        if (ret == ESP_OK) {
            const char *phase = handle_received_packet(&status);
            show_radio_status(&status, phase, false);
            next_ping = schedule_next_ping();
            continue;
        }

        if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "receive failed: state=%d payload=%s", status.state, status.last_payload);
            show_radio_status(&status, "rx fail", false);
            vTaskDelay(pdMS_TO_TICKS(500));
            next_ping = schedule_next_ping();
            continue;
        }

        if (!ping_is_due(next_ping)) {
            continue;
        }

        ret = board_radio_ping(&status);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "transmit failed: state=%d", status.state);
            show_radio_status(&status, "tx fail", false);
            next_ping = schedule_next_ping();
            continue;
        }

        ESP_LOGI(TAG, "tx payload=%s state=%d", status.last_payload, status.state);
        ret = board_radio_receive(&status, REPLY_WINDOW_MS);
        const char *phase = ret == ESP_OK ? handle_received_packet(&status) : "pong wait";
        ESP_LOGI(TAG, "reply state=%d payload=%s", status.state, status.last_payload);
        show_radio_status(&status, phase, false);
        next_ping = schedule_next_ping();
    }
}
