/*
 * Encodes the documented 15-byte uplink and renders downlinks as bounded
 * hexadecimal log lines. Received bytes never modify radio configuration.
 */
#include "lorawan_payload.h"

#include <algorithm>
#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lorawan_config.h"

static const char *TAG = "lorawan_payload";

// Store an unsigned 32-bit field in network byte order.
static void put_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

size_t lorawan_build_uplink_payload(uint8_t *payload,
                                    size_t capacity,
                                    uint32_t sequence,
                                    bool restored_session,
                                    bool had_downlink)
{
    if (payload == nullptr || capacity < LORAWAN_UPLINK_PAYLOAD_SIZE) {
        return 0;
    }

    // Flags describe how this boot session produced the current payload.
    uint8_t flags = 0;
    if (LORAWAN_CONFIRMED_UPLINK) {
        flags |= 0x01U;
    }
    if (restored_session) {
        flags |= 0x02U;
    }
    if (had_downlink) {
        flags |= 0x04U;
    }

    payload[0] = 0x01U;
    payload[1] = flags;
    put_u32_be(&payload[2], sequence);
    put_u32_be(&payload[6],
               (uint32_t)(esp_timer_get_time() / UINT64_C(1000000)));
    put_u32_be(&payload[10], esp_get_free_heap_size());
    payload[14] = (uint8_t)esp_reset_reason();
    return LORAWAN_UPLINK_PAYLOAD_SIZE;
}

// Bound each log record to 32 payload bytes to avoid large stack buffers.
static void log_hex(const uint8_t *data, size_t length)
{
    static const char hex[] = "0123456789ABCDEF";

    for (size_t offset = 0; offset < length; offset += 32U) {
        const size_t chunk = std::min(length - offset, (size_t)32U);
        char line[32U * 2U + 1U];

        for (size_t i = 0; i < chunk; ++i) {
            line[i * 2U] = hex[data[offset + i] >> 4U];
            line[i * 2U + 1U] = hex[data[offset + i] & 0x0FU];
        }
        line[chunk * 2U] = '\0';

        ESP_LOGI(TAG, "downlink hex[%u..%u]: %s",
                 (unsigned)offset, (unsigned)(offset + chunk - 1U), line);
    }
}

void lorawan_log_downlink(const uint8_t *data,
                          size_t length,
                          const LoRaWANEvent_t &event,
                          int16_t rx_window,
                          float rssi,
                          float snr)
{
    ESP_LOGI(TAG,
             "downlink RX%d port=%u len=%u FCnt=%" PRIu32
             " RSSI=%.1f dBm SNR=%.1f dB confirmed=%d pending=%d",
             rx_window, (unsigned)event.fPort, (unsigned)length, event.fCnt,
             (double)rssi, (double)snr, event.confirmed, event.frmPending);

    if (length > 0U) {
        log_hex(data, length);
    } else {
        ESP_LOGI(TAG, "downlink contained MAC commands only");
    }

    // Only the dedicated command port is interpreted; all other data is logged.
    if (event.fPort != LORAWAN_COMMAND_PORT || length == 0U) {
        return;
    }

    // Command 0x01 is intentionally informational and has no side effects.
    if (data[0] == 0x01U) {
        ESP_LOGI(TAG,
                 "demo command 0x01 received; no configuration was changed");
    } else {
        ESP_LOGW(TAG, "unknown demo command 0x%02X ignored",
                 (unsigned)data[0]);
    }
}
