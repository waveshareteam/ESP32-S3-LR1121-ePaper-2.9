#include "lorawan_flow.h"

#include <algorithm>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "multicast_config.h"
#include "multicast_session.h"

/*
 * Device activation, downlink handling, periodic unicast uplinks, and the
 * continuous Class C receive loop. All radio calls remain on one task.
 */
namespace {

constexpr char kTag[] = "lorawan_multicast";
constexpr uint32_t kSendFailuresBeforeRejoin = 3U;

/* Writes a compact payload dump without allocating a large log buffer. */
void log_payload_hex(const char *cast_name,
                     const uint8_t *data,
                     size_t length)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t offset = 0U; offset < length; offset += 32U) {
        const size_t count = std::min(length - offset, size_t{32U});
        char line[65] = {};
        for (size_t i = 0U; i < count; ++i) {
            line[i * 2U] = hex[data[offset + i] >> 4U];
            line[i * 2U + 1U] = hex[data[offset + i] & 0x0FU];
        }
        ESP_LOGI(kTag, "%s HEX[%u..%u]: %s",
                 cast_name,
                 static_cast<unsigned>(offset),
                 static_cast<unsigned>(offset + count - 1U),
                 line);
    }
}

/* Replaces non-printable bytes so logs cannot emit terminal control data. */
void log_payload_text(const char *cast_name,
                      const uint8_t *data,
                      size_t length)
{
    char text[MULTICAST_LOG_SAFE_TEXT_LIMIT + 1U] = {};
    const size_t count =
        std::min(length,
                 static_cast<size_t>(MULTICAST_LOG_SAFE_TEXT_LIMIT));
    for (size_t i = 0U; i < count; ++i) {
        text[i] = data[i] >= 32U && data[i] <= 126U
                      ? static_cast<char>(data[i])
                      : '.';
    }
    ESP_LOGI(kTag, "%s TEXT%s: %s",
             cast_name,
             length > count ? " (truncated)" : "",
             text);
}

void log_validated_payload(const char *cast_name,
                           const uint8_t *data,
                           size_t length)
{
    if (length == 0U) {
        ESP_LOGI(kTag, "%s payload is empty", cast_name);
        return;
    }
    if (MULTICAST_LOG_HEX_ENABLED != 0U) {
        log_payload_hex(cast_name, data, length);
    }
    if (MULTICAST_LOG_TEXT_ENABLED != 0U) {
        log_payload_text(cast_name, data, length);
    }
}

/*
 * Validates the receive path before exposing payload bytes to the application.
 * Multicast FCntDown is committed first so a reset cannot replay accepted data.
 */
LoRaWANLoopDecision handle_downlink(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const uint8_t *data,
    size_t length,
    const LoRaWANEvent_t &event,
    int16_t rx_window,
    float rssi,
    float snr,
    bool unicast_state_already_saved,
    const MulticastStoreConfig &store_config,
    MulticastCounterState *counter,
    bool *multicast_active)
{
    if (length > RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE) {
        ESP_LOGE(kTag, "downlink length %u exceeds buffer capacity %u",
                 static_cast<unsigned>(length),
                 static_cast<unsigned>(
                     RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE));
        runtime.update_ui("ERROR", "downlink too long",
                          RADIOLIB_ERR_PACKET_TOO_LONG);
        return LoRaWANLoopDecision::kContinue;
    }

    const char *cast_name = event.multicast ? "MULTICAST" : "UNICAST";
    if (event.multicast) {
        if (event.mcGroupId != MULTICAST_GROUP_ID) {
            ESP_LOGE(kTag,
                     "MULTICAST event used unexpected slot %u",
                     static_cast<unsigned>(event.mcGroupId));
            runtime.update_ui("ERROR", "unknown multicast slot",
                              RADIOLIB_ERR_INVALID_MULTICAST_GROUP);
            return LoRaWANLoopDecision::kContinue;
        }
        const CounterDecision decision = accept_multicast_counter(
            runtime, node, store_config, event.fCnt,
            counter, multicast_active);
        if (decision == CounterDecision::kReplay) {
            return LoRaWANLoopDecision::kContinue;
        }
        if (decision == CounterDecision::kRejoin) {
            return LoRaWANLoopDecision::kRejoin;
        }
    } else if (!unicast_state_already_saved) {
        if (!persist_unicast_with_pause(
                runtime, node, true, *counter, multicast_active)) {
            return LoRaWANLoopDecision::kRejoin;
        }
    }

    ESP_LOGI(kTag,
             "%s RX%d FPort=%u len=%u FCntDown=%" PRIu32
             " freq=%.4fMHz DR%u RSSI=%.1fdBm SNR=%.1fdB",
             cast_name, rx_window,
             static_cast<unsigned>(event.fPort),
             static_cast<unsigned>(length),
             event.fCnt,
             static_cast<double>(event.freq),
             static_cast<unsigned>(event.datarate),
             static_cast<double>(rssi),
             static_cast<double>(snr));
    log_validated_payload(cast_name, data, length);

    MulticastUiState &ui = runtime.ui();
    ui.last_port = event.fPort;
    ui.signal_valid = true;
    ui.rssi = rssi;
    ui.snr = snr;
    if (event.multicast) {
        ++ui.multicast_rx_count;
        if (event.fPort != MULTICAST_DOWNLINK_PORT) {
            ESP_LOGW(kTag,
                     "MULTICAST FPort %u ignored; configured application "
                     "FPort is %u",
                     static_cast<unsigned>(event.fPort),
                     static_cast<unsigned>(MULTICAST_DOWNLINK_PORT));
            runtime.update_ui(
                "RX MULTICAST", "unknown port ignored", rx_window);
        } else if (length == 0U) {
            runtime.update_ui("RX MULTICAST", "empty payload", rx_window);
        } else {
            runtime.update_ui(
                "RX MULTICAST", "payload accepted", rx_window);
        }
    } else {
        ++ui.unicast_rx_count;
        if (event.fPort != LORAWAN_UNICAST_DOWNLINK_PORT) {
            ESP_LOGW(kTag,
                     "UNICAST FPort %u ignored; configured application "
                     "FPort is %u",
                     static_cast<unsigned>(event.fPort),
                     static_cast<unsigned>(
                         LORAWAN_UNICAST_DOWNLINK_PORT));
            runtime.update_ui(
                "RX UNICAST", "unknown port ignored", rx_window);
        } else if (length == 0U) {
            runtime.update_ui("RX UNICAST", "empty payload", rx_window);
        } else {
            runtime.update_ui("RX UNICAST", "payload accepted", rx_window);
        }
    }
    return LoRaWANLoopDecision::kContinue;
}

uint32_t join_retry_wait_ms(LoRaWANNode &node)
{
    return std::max(
        static_cast<uint32_t>(node.timeUntilUplink()),
        seconds_to_ms(LORAWAN_JOIN_RETRY_SECONDS));
}

void put_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value >> 24U);
    output[1] = static_cast<uint8_t>(value >> 16U);
    output[2] = static_cast<uint8_t>(value >> 8U);
    output[3] = static_cast<uint8_t>(value);
}

/* Encodes public runtime counters for the device's own unicast uplink. */
size_t build_uplink(RadioRuntime &runtime,
                    uint8_t *payload,
                    size_t capacity,
                    const MulticastCounterState &counter)
{
    constexpr size_t kPayloadLength = 15U;
    if (payload == nullptr || capacity < kPayloadLength) {
        return 0U;
    }

    const MulticastUiState &ui = runtime.ui();
    payload[0] = 0x01U;
    payload[1] =
        (ui.session_restored ? 0x01U : 0U) |
        (counter.counter_valid ? 0x02U : 0U);
    put_u32_be(&payload[2],
               static_cast<uint32_t>(
                   esp_timer_get_time() / 1000000LL));
    put_u32_be(&payload[6], ui.multicast_rx_count);
    put_u32_be(&payload[10], ui.unicast_rx_count);
    payload[14] = MULTICAST_GROUP_ID;
    return kPayloadLength;
}

bool post_uplink_receive_error(int16_t state)
{
    return state == RADIOLIB_ERR_DOWNLINK_MALFORMED ||
           state == RADIOLIB_ERR_NO_RX_WINDOW;
}

bool restore_multicast_after_unicast(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastCounterState &counter,
    bool resume_multicast,
    bool *multicast_active)
{
    if (!resume_multicast) {
        return true;
    }
    if (!restart_multicast_until_ready(
            runtime, node, counter, multicast_active)) {
        return false;
    }
    ESP_LOGI(kTag,
             "MULTICAST RxC restored after unicast TX/RX1/RX2");
    return true;
}

/*
 * Pauses RxC, completes the device's Class A transaction, persists the
 * unicast session, then immediately restores the multicast receive window.
 */
LoRaWANLoopDecision send_unicast_uplink(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    LR1121 &radio,
    const MulticastStoreConfig &store_config,
    MulticastCounterState *counter,
    bool *multicast_active,
    unsigned *consecutive_failures)
{
    uint8_t uplink[15] = {};
    const size_t uplink_length =
        build_uplink(runtime, uplink, sizeof(uplink), *counter);
    if (uplink_length == 0U) {
        runtime.hold("ERROR", "uplink encode", ESP_ERR_INVALID_SIZE);
    }

    uint8_t downlink[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE] = {};
    size_t downlink_length = 0U;
    LoRaWANEvent_t uplink_event = {};
    LoRaWANEvent_t downlink_event = {};
    float rssi = 0.0f;
    float snr = 0.0f;

    const bool resume_multicast = *multicast_active;
    if (resume_multicast) {
        if (!stop_multicast(runtime, node, multicast_active)) {
            runtime.hold("ERROR", "pause multicast",
                         RADIOLIB_ERR_INVALID_MULTICAST_GROUP);
        }
        ESP_LOGI(kTag,
                 "MULTICAST RxC paused for unicast TX/RX1/RX2");
    }

    if (!runtime.begin(RadioState::kTx)) {
        runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
    }
    const uint32_t fcnt_before = node.getFCntUp();
    uint32_t fcnt_after = fcnt_before;
    bool send_receive_called = false;
    int16_t state = node.apply_verified_policy();
    if (state == RADIOLIB_ERR_NONE) {
        send_receive_called = true;
        state = node.sendReceive(
            uplink, uplink_length, LORAWAN_UPLINK_PORT,
            downlink, &downlink_length, false,
            &uplink_event, &downlink_event);
        fcnt_after = node.getFCntUp();
    }
    if (state > RADIOLIB_ERR_NONE) {
        rssi = radio.getRSSI();
        snr = radio.getSNR();
    }
    runtime.end(RadioState::kIdle);

    /*
     * RadioLib can advance FCntUp even when sendReceive() returns an error.
     * Save after every actual call, before interpreting its return value.
     */
    if (send_receive_called) {
        if (!persist_unicast_with_pause(
                runtime, node, true, *counter, multicast_active)) {
            return LoRaWANLoopDecision::kRejoin;
        }
        ESP_LOGI(kTag,
                 "unicast session saved after sendReceive: "
                 "FCntUp(before=%" PRIu32 ", after=%" PRIu32
                 ", event=%" PRIu32 ") state=%d",
                 fcnt_before, fcnt_after, uplink_event.fCnt, state);
    }

    if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
        ESP_LOGE(kTag, "unicast session is no longer joined");
        runtime.update_ui("ERROR", "unicast not joined", state);
        return LoRaWANLoopDecision::kRejoin;
    }

    if (!restore_multicast_after_unicast(
            runtime, node, *counter,
            resume_multicast, multicast_active)) {
        return LoRaWANLoopDecision::kRejoin;
    }

    MulticastUiState &ui = runtime.ui();
    if (state >= RADIOLIB_ERR_NONE) {
        ++ui.tx_count;
        *consecutive_failures = 0U;
        ESP_LOGI(kTag,
                 "UNICAST TX own-session FPort=%u len=%u "
                 "FCntUp=%" PRIu32 " state=%d",
                 static_cast<unsigned>(LORAWAN_UPLINK_PORT),
                 static_cast<unsigned>(uplink_length),
                 uplink_event.fCnt, state);

        if (state > RADIOLIB_ERR_NONE) {
            return handle_downlink(
                runtime, node, downlink, downlink_length,
                downlink_event, state, rssi, snr, true,
                store_config, counter, multicast_active);
        }
        ui.last_port = LORAWAN_UPLINK_PORT;
        runtime.update_ui("TX", "unicast uplink OK", state);
        return LoRaWANLoopDecision::kContinue;
    }

    /* Scheduler deferral and RX1/RX2 parse failures are not hard TX errors. */
    if (state == RADIOLIB_ERR_UPLINK_UNAVAILABLE) {
        *consecutive_failures = 0U;
        ESP_LOGW(kTag, "UNICAST uplink deferred by duty-cycle scheduler");
        runtime.update_ui("TX", "uplink deferred", state);
        return LoRaWANLoopDecision::kContinue;
    }

    if (post_uplink_receive_error(state)) {
        ++ui.tx_count;
        *consecutive_failures = 0U;
        ESP_LOGW(kTag,
                 "UNICAST TX completed, but RX1/RX2 processing returned "
                 "RadioLib %d; session kept and RxC restored",
                 state);
        runtime.update_ui("TX", "RX1/RX2 error", state);
        return LoRaWANLoopDecision::kContinue;
    }

    ++*consecutive_failures;
    ESP_LOGE(kTag,
             "UNICAST TX hard error: RadioLib %d consecutive=%u; "
             "session counter saved and RxC restored",
             state, *consecutive_failures);
    runtime.update_ui("ERROR", "unicast TX hard error", state);

    return *consecutive_failures >= kSendFailuresBeforeRejoin
               ? LoRaWANLoopDecision::kRejoin
               : LoRaWANLoopDecision::kContinue;
}

}  // namespace

bool lorawan_activate_unicast(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastCounterState &counter,
    bool *session_restored)
{
    *session_restored = false;
    for (unsigned attempt = 1U;
         attempt <= LORAWAN_JOIN_MAX_ATTEMPTS;
         ++attempt) {
        char detail[32] = {};
        snprintf(detail, sizeof(detail), "OTAA %u/%u",
                 attempt,
                 static_cast<unsigned>(LORAWAN_JOIN_MAX_ATTEMPTS));
        runtime.update_ui("JOINING", detail, 0);
        ESP_LOGI(kTag, "OTAA attempt %u/%u",
                 attempt,
                 static_cast<unsigned>(LORAWAN_JOIN_MAX_ATTEMPTS));

        if (!runtime.begin(RadioState::kJoining)) {
            runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
        }
        int16_t state = node.apply_verified_policy();
        if (state == RADIOLIB_ERR_NONE) {
            state = node.activateOTAA();
        }
        runtime.end(RadioState::kIdle);

        /* Every JoinRequest consumes DevNonce, even without a JoinAccept. */
        if (!persist_unicast_with_pause(
                runtime, node, false, counter, nullptr)) {
            return false;
        }

        const bool joined =
            node.isActivated() &&
            (state == RADIOLIB_LORAWAN_NEW_SESSION ||
             state == RADIOLIB_LORAWAN_SESSION_RESTORED);
        if (joined) {
            if (!runtime.begin(RadioState::kRecovery)) {
                runtime.hold(
                    "ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
            }
            const int16_t policy_state = node.apply_verified_policy();
            runtime.end(RadioState::kIdle);
            if (policy_state != RADIOLIB_ERR_NONE) {
                ESP_LOGE(kTag,
                         "reapply verified channel policy failed: %d",
                         policy_state);
                runtime.update_ui("ERROR", "channel policy", policy_state);
                return false;
            }
            if (!persist_unicast_with_pause(
                    runtime, node, true, counter, nullptr)) {
                return false;
            }

            *session_restored =
                state == RADIOLIB_LORAWAN_SESSION_RESTORED;
            runtime.ui().session_restored = *session_restored;
            ESP_LOGI(kTag,
                     "unicast OTAA %s; own session remains Class A",
                     *session_restored ? "session restored" : "joined");
            runtime.update_ui(
                *session_restored ? "SESSION RESTORED" : "JOINED",
                "unicast Class A", state);
            return true;
        }

        ESP_LOGW(kTag, "OTAA attempt failed: RadioLib %d", state);
        runtime.update_ui("ERROR", "OTAA retry", state);
        if (attempt < LORAWAN_JOIN_MAX_ATTEMPTS) {
            delay_ms(join_retry_wait_ms(node));
        }
    }
    return false;
}

LoRaWANLoopDecision lorawan_run_active_loop(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    LR1121 &radio,
    const MulticastStoreConfig &store_config,
    MulticastCounterState *counter,
    bool *multicast_active)
{
    int64_t next_uplink_ms =
        monotonic_ms() +
        static_cast<int64_t>(
            seconds_to_ms(LORAWAN_UPLINK_INTERVAL_SECONDS));
    unsigned receive_errors = 0U;
    unsigned send_failures = 0U;

    /* Class C polling keeps RxC active except during serialized uplinks. */
    while (true) {
        uint8_t downlink[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE] = {};
        size_t downlink_length = 0U;
        LoRaWANEvent_t event = {};
        float rssi = 0.0f;
        float snr = 0.0f;

        if (!runtime.begin(RadioState::kClassC)) {
            runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
        }
        const int16_t state = node.getDownlinkClassC(
            downlink, &downlink_length, &event);
        if (state > RADIOLIB_ERR_NONE) {
            rssi = radio.getRSSI();
            snr = radio.getSNR();
        }
        runtime.end(RadioState::kClassC);

        if (state > RADIOLIB_ERR_NONE) {
            receive_errors = 0U;
            const LoRaWANLoopDecision decision = handle_downlink(
                runtime, node, downlink, downlink_length,
                event, state, rssi, snr, false,
                store_config, counter, multicast_active);
            if (decision == LoRaWANLoopDecision::kRejoin) {
                return decision;
            }
        } else if (state < RADIOLIB_ERR_NONE) {
            ++receive_errors;
            ESP_LOGE(kTag,
                     "Class C parse error: RadioLib %d consecutive=%u",
                     state, receive_errors);
            runtime.update_ui("ERROR", "Class C RX error", state);
            if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
                return LoRaWANLoopDecision::kRejoin;
            }
            if (receive_errors >= MULTICAST_RX_ERROR_RESTART_COUNT) {
                if (!restart_multicast_until_ready(
                        runtime, node, *counter, multicast_active)) {
                    return LoRaWANLoopDecision::kRejoin;
                }
                receive_errors = 0U;
            } else {
                delay_ms(seconds_to_ms(
                    MULTICAST_ERROR_BACKOFF_SECONDS));
            }
        }

        const int64_t now_ms = monotonic_ms();
        if (now_ms >= next_uplink_ms) {
            const uint32_t legal_wait_ms =
                static_cast<uint32_t>(node.timeUntilUplink());
            if (legal_wait_ms > 0U) {
                next_uplink_ms =
                    now_ms + static_cast<int64_t>(legal_wait_ms);
            } else {
                const LoRaWANLoopDecision decision = send_unicast_uplink(
                    runtime, node, radio, store_config, counter,
                    multicast_active, &send_failures);
                if (decision == LoRaWANLoopDecision::kRejoin) {
                    return decision;
                }
                next_uplink_ms =
                    monotonic_ms() +
                    static_cast<int64_t>(
                        seconds_to_ms(
                            LORAWAN_UPLINK_INTERVAL_SECONDS));
            }
        }

        delay_ms(MULTICAST_CLASS_C_POLL_MS);
    }
}
