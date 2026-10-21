/*
 * Implements bounded OTAA retries, periodic Class A transactions, session
 * persistence, and controlled rejoin recovery.
 *
 * RadioLib's activateOTAA() and sendReceive() calls are synchronous. No NVS
 * access or e-paper refresh is performed while their RX windows are open.
 */
#include "lorawan_runtime.h"

#include <algorithm>
#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lorawan_config.h"
#include "lorawan_payload.h"
#include "lorawan_session.h"

static const char *TAG = "lr1121_lorawan";

// Counters in this structure are application/UI state, not LoRaWAN frame state.
struct RuntimeState {
    uint32_t next_sequence;
    uint32_t uplink_count;
    uint32_t downlink_count;
    unsigned consecutive_failures;
    bool restored_session;
    uint8_t default_channel_seen_mask;
    uint32_t other_channel_uplinks;
};

#if !LORAWAN_SINGLE_CHANNEL_MODE && \
    (LORAWAN_REGION_SELECTION == LORAWAN_REGION_EU868)
// EU868 begins with these three mandatory channels before network additions.
static constexpr float EU868_DEFAULT_CHANNELS_MHZ[] = {
    868.1f, 868.3f, 868.5f,
};

static bool frequency_matches(float actual_mhz, float expected_mhz)
{
    constexpr float tolerance_mhz = 0.0002f;
    return actual_mhz >= expected_mhz - tolerance_mhz &&
           actual_mhz <= expected_mhz + tolerance_mhz;
}

static uint8_t count_seen_default_channels(uint8_t mask)
{
    uint8_t count = 0U;
    for (uint8_t bit = 0U; bit < 3U; ++bit) {
        if ((mask & (uint8_t)(1U << bit)) != 0U) {
            ++count;
        }
    }
    return count;
}

/*
 * Track observed successful uplinks without changing RadioLib's channel
 * selection. Network-added EU868 frequencies are counted separately.
 */
static void record_uplink_channel(RuntimeState &runtime,
                                  lorawan_ui_state_t &status,
                                  float frequency_mhz)
{
    bool default_channel = false;
    for (uint8_t index = 0U; index < 3U; ++index) {
        if (frequency_matches(frequency_mhz,
                              EU868_DEFAULT_CHANNELS_MHZ[index])) {
            runtime.default_channel_seen_mask |= (uint8_t)(1U << index);
            default_channel = true;
            break;
        }
    }
    if (!default_channel) {
        ++runtime.other_channel_uplinks;
    }

    status.default_channels_seen =
        count_seen_default_channels(runtime.default_channel_seen_mask);
    ESP_LOGI(TAG,
             "EU868 channel coverage: default-seen=%u/3 mask=0x%02X "
             "other-uplinks=%" PRIu32,
             (unsigned)status.default_channels_seen,
             (unsigned)runtime.default_channel_seen_mask,
             runtime.other_channel_uplinks);
}
#else
static void record_uplink_channel(RuntimeState &,
                                  lorawan_ui_state_t &,
                                  float)
{
}
#endif

// Keep logs readable while retaining the numeric RadioLib error code.
const char *radiolib_state_name(int16_t state)
{
    switch (state) {
    case RADIOLIB_ERR_NONE:
        return "none";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
        return "chip-not-found";
    case RADIOLIB_ERR_PACKET_TOO_LONG:
        return "packet-too-long";
    case RADIOLIB_ERR_RX_TIMEOUT:
        return "rx-timeout";
    case RADIOLIB_ERR_MIC_MISMATCH:
        return "mic-mismatch";
    case RADIOLIB_ERR_INVALID_FREQUENCY:
        return "invalid-frequency";
    case RADIOLIB_ERR_INVALID_OUTPUT_POWER:
        return "invalid-output-power";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:
        return "network-not-joined";
    case RADIOLIB_ERR_DOWNLINK_MALFORMED:
        return "downlink-malformed";
    case RADIOLIB_ERR_INVALID_PORT:
        return "invalid-port";
    case RADIOLIB_ERR_NO_RX_WINDOW:
        return "no-rx-window";
    case RADIOLIB_ERR_UPLINK_UNAVAILABLE:
        return "uplink-unavailable";
    case RADIOLIB_ERR_JOIN_NONCE_INVALID:
        return "join-nonce-invalid";
    case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:
        return "dwell-time-exceeded";
    case RADIOLIB_ERR_CHECKSUM_MISMATCH:
        return "checksum-mismatch";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT:
        return "no-join-accept";
    case RADIOLIB_LORAWAN_SESSION_RESTORED:
        return "session-restored";
    case RADIOLIB_LORAWAN_NEW_SESSION:
        return "new-session";
    case RADIOLIB_ERR_NONCES_DISCARDED:
        return "nonces-discarded";
    case RADIOLIB_ERR_SESSION_DISCARDED:
        return "session-discarded";
    default:
        return "other";
    }
}

// Saturation prevents a large configured interval from wrapping.
uint32_t lorawan_seconds_to_ms(uint32_t seconds)
{
    const uint64_t value = (uint64_t)seconds * UINT64_C(1000);
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

void lorawan_delay_ms(uint32_t duration_ms)
{
    // Short chunks avoid tick-conversion overflow and remain watchdog friendly.
    while (duration_ms > 0U) {
        const uint32_t chunk = std::min(duration_ms, UINT32_C(60000));
        vTaskDelay(pdMS_TO_TICKS(chunk));
        duration_ms -= chunk;
    }
}

// Put LR1121 into sleep before application, retry, or regulatory waits.
static void wait_with_radio_sleep(LR1121 &radio, uint32_t duration_ms)
{
    if (duration_ms == 0U) {
        return;
    }

    const int16_t state = radio.sleep();
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "LR1121 sleep returned %d (%s)",
                 state, radiolib_state_name(state));
    }
    lorawan_delay_ms(duration_ms);
}

// A configuration fault requires new firmware/provisioning, not a fast retry.
[[noreturn]] void lorawan_wait_for_configuration(void)
{
    while (true) {
        lorawan_delay_ms(UINT32_C(60000));
    }
}

/*
 * Persistence is part of the radio transaction contract. If NVS fails, pause
 * all further transmissions until the advanced nonce/counter state is durable.
 */
static void save_state_until_done(LoRaWANNode &node,
                                  LR1121 &radio,
                                  bool include_session)
{
    while (true) {
        const esp_err_t err =
            include_session ? lorawan_session_save(node)
                            : lorawan_session_save_nonces(node);
        if (err == ESP_OK) {
            return;
        }

        ESP_LOGE(TAG,
                 "LoRaWAN NVS commit failed: %s; radio operations paused",
                 esp_err_to_name(err));
        wait_with_radio_sleep(
            radio,
            lorawan_seconds_to_ms(LORAWAN_PERSISTENCE_RETRY_SECONDS));
    }
}

// Clear only the session blob; the OTAA nonce history must be retained.
static void clear_saved_session_until_done(LR1121 &radio)
{
    while (true) {
        const esp_err_t err = lorawan_session_clear_saved_session();
        if (err == ESP_OK) {
            return;
        }

        ESP_LOGE(TAG,
                 "LoRaWAN session clear failed: %s; radio operations paused",
                 esp_err_to_name(err));
        wait_with_radio_sleep(
            radio,
            lorawan_seconds_to_ms(LORAWAN_PERSISTENCE_RETRY_SECONDS));
    }
}

// Exponential backoff plus up to 25 percent jitter spreads OTAA retries.
static uint32_t join_backoff_seconds(unsigned attempt)
{
    uint32_t backoff = LORAWAN_JOIN_BACKOFF_BASE_SECONDS;
    for (unsigned i = 1; i < attempt; ++i) {
        if (backoff >= LORAWAN_JOIN_BACKOFF_MAX_SECONDS / 2U) {
            backoff = LORAWAN_JOIN_BACKOFF_MAX_SECONDS;
            break;
        }
        backoff *= 2U;
    }
    backoff = std::min(backoff,
                       (uint32_t)LORAWAN_JOIN_BACKOFF_MAX_SECONDS);

    const uint32_t jitter_range = backoff / 4U + 1U;
    const uint32_t jitter = esp_random() % jitter_range;
    return std::min(backoff + jitter,
                    (uint32_t)LORAWAN_JOIN_BACKOFF_MAX_SECONDS);
}

/*
 * Run one bounded OTAA batch. A restored RadioLib session is activated through
 * the same API and is reported separately from a newly joined session.
 */
static bool join_network(ConfiguredLoRaWANNode &node,
                         LR1121 &radio,
                         RuntimeState &runtime,
                         LoRaWANStatusUi &ui)
{
    lorawan_ui_state_t &status = ui.state();
    runtime.restored_session = false;

    for (unsigned attempt = 1U;
         attempt <= LORAWAN_JOIN_MAX_ATTEMPTS;
         ++attempt) {
        // Reapply the channel policy before every join transmission.
        const int16_t policy_state = lorawan_apply_network_policy(node);
        if (policy_state != RADIOLIB_ERR_NONE) {
            ESP_LOGE(TAG, "prepare OTAA channel policy failed: %d (%s)",
                     policy_state, radiolib_state_name(policy_state));
            ui.update("POLICY ERROR",
                      radiolib_state_name(policy_state), policy_state);
            return false;
        }

        ESP_LOGI(TAG, "OTAA attempt %u/%u",
                 attempt, (unsigned)LORAWAN_JOIN_MAX_ATTEMPTS);

        char join_status[24];
        snprintf(join_status, sizeof(join_status), "JOINING %u/%u",
                 attempt, (unsigned)LORAWAN_JOIN_MAX_ATTEMPTS);
        status.fcnt_up = node.getFCntUp();
        status.next_sequence = runtime.next_sequence;
        // Finish the SPI-based display refresh before opening join RX windows.
        ui.update(join_status, "OTAA request", RADIOLIB_ERR_NONE);

        LoRaWANJoinEvent_t join_event = {};
        // Restore a valid session, or transmit JoinRequest and wait for JoinAccept.
        const int16_t state = node.activateOTAA(&join_event);
        const bool success =
            node.isActivated() &&
            (state == RADIOLIB_LORAWAN_NEW_SESSION ||
             state == RADIOLIB_LORAWAN_SESSION_RESTORED);

        float join_rssi = 0.0f;
        float join_snr = 0.0f;
        if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
            // Read signal metrics immediately, before NVS or display uses SPI.
            join_rssi = radio.getRSSI();
            join_snr = radio.getSNR();
        }

        // A transmitted JoinRequest always advances DevNonce.
        // Save after every result so NVS stays synchronized with RadioLib.
        save_state_until_done(node, radio, false);

        if (success) {
            // JoinAccept or restore may rebuild channel state, so apply it again.
            const int16_t joined_policy =
                lorawan_apply_network_policy(node);
            if (joined_policy != RADIOLIB_ERR_NONE) {
                ESP_LOGE(TAG,
                         "reapply channel policy after JoinAccept failed: "
                         "%d (%s)",
                         joined_policy, radiolib_state_name(joined_policy));
                ui.update("POLICY ERROR",
                          radiolib_state_name(joined_policy), joined_policy);
                return false;
            }

            // Persist session keys and frame counters before normal uplinks start.
            save_state_until_done(node, radio, true);
            runtime.restored_session =
                state == RADIOLIB_LORAWAN_SESSION_RESTORED;

            ESP_LOGI(TAG,
                     "OTAA %s (Class A): state=%d DevAddr=%08" PRIX32
                     " FCntUp=%" PRIu32,
                     runtime.restored_session ? "session restored" : "joined",
                     state, node.getDevAddr(), node.getFCntUp());

            status.fcnt_up = node.getFCntUp();
            if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
                status.signal_valid = true;
                status.rssi = join_rssi;
                status.snr = join_snr;
            }
            ui.update(runtime.restored_session
                          ? "SESSION RESTORED"
                          : "JOINED",
                      radiolib_state_name(state), state);
            return true;
        }

        ESP_LOGW(TAG, "OTAA failed: %d (%s)",
                 state, radiolib_state_name(state));
        status.fcnt_up = node.getFCntUp();
        ui.update("JOIN RETRY", radiolib_state_name(state), state);

        if (attempt == LORAWAN_JOIN_MAX_ATTEMPTS) {
            break;
        }

        // Honor both application backoff and RadioLib's regulatory scheduler.
        const uint32_t backoff_ms =
            lorawan_seconds_to_ms(join_backoff_seconds(attempt));
        const uint32_t legal_wait_ms = node.timeUntilUplink();
        const uint32_t wait_ms = std::max(backoff_ms, legal_wait_ms);
        ESP_LOGI(TAG, "next OTAA attempt in %" PRIu32 " ms", wait_ms);
        wait_with_radio_sleep(radio, wait_ms);
    }
    return false;
}

/*
 * Recover from a lost session without erasing DevNonce history. The order is:
 * clear RadioLib session, erase sess_sc, save nonce, reapply policy, then wait.
 */
static void schedule_rejoin(ConfiguredLoRaWANNode &node,
                            LR1121 &radio,
                            RuntimeState &runtime,
                            LoRaWANStatusUi &ui,
                            int16_t send_state)
{
    ESP_LOGW(TAG,
             "clearing session for controlled OTAA recovery; "
             "nonces are retained");

    node.clearSession();
    clear_saved_session_until_done(radio);
    save_state_until_done(node, radio, false);

    const int16_t policy_state = lorawan_apply_network_policy(node);
    if (policy_state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "reapply network policy failed: %d (%s)",
                 policy_state, radiolib_state_name(policy_state));
        ui.update("POLICY ERROR",
                  radiolib_state_name(policy_state), policy_state);
        lorawan_wait_for_configuration();
    }

    ESP_LOGI(TAG, "saved session cleared; OTAA recovery scheduled");
    runtime.restored_session = false;
    ui.state().fcnt_up = node.getFCntUp();
    ui.update("REJOIN WAIT", radiolib_state_name(send_state), send_state);

    wait_with_radio_sleep(
        radio, lorawan_seconds_to_ms(LORAWAN_SEND_RECOVERY_SECONDS));
}

/*
 * Main workflow. All operations remain serialized in this task to protect the
 * shared SPI bus and RadioLib's Class A timing.
 */
[[noreturn]] void lorawan_runtime_run(ConfiguredLoRaWANNode &node,
                                      LR1121 &radio,
                                      LoRaWANStatusUi &ui)
{
    RuntimeState runtime = {};
    lorawan_ui_state_t &status = ui.state();
    status.fcnt_up = node.getFCntUp();
    status.next_sequence = runtime.next_sequence;

    while (true) {
        // A pending restored session also becomes active through this path.
        if (!node.isActivated()) {
            if (!join_network(node, radio, runtime, ui)) {
                ESP_LOGW(TAG,
                         "OTAA attempt limit reached; cooling down for %u seconds",
                         (unsigned)LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS);
                ui.update("JOIN COOLDOWN", "attempt limit", status.last_code);
                wait_with_radio_sleep(
                    radio,
                    lorawan_seconds_to_ms(
                        LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS));
                continue;
            }
            runtime.consecutive_failures = 0;
        }

        // Build the documented fixed-size application payload for this attempt.
        uint8_t uplink[LORAWAN_UPLINK_PAYLOAD_SIZE] = {};
        const uint32_t sequence = runtime.next_sequence;
        const size_t uplink_length = lorawan_build_uplink_payload(
            uplink, sizeof(uplink), sequence, runtime.restored_session,
            runtime.downlink_count > 0U);
        if (uplink_length == 0U) {
            ESP_LOGE(TAG, "uplink payload encoding failed");
            lorawan_wait_for_configuration();
        }

        uint8_t downlink[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE] = {};
        size_t downlink_length = 0;
        LoRaWANEvent_t uplink_event = {};
        LoRaWANEvent_t downlink_event = {};

        // Apply power, ADR, and channel policy immediately before each uplink.
        const int16_t policy_state = lorawan_apply_network_policy(node);
        if (policy_state != RADIOLIB_ERR_NONE) {
            ESP_LOGE(TAG, "prepare uplink channel policy failed: %d (%s)",
                     policy_state, radiolib_state_name(policy_state));
            ui.update("POLICY ERROR",
                      radiolib_state_name(policy_state), policy_state);
            lorawan_wait_for_configuration();
        }

        // RadioLib accounts for duty-cycle and dwell-time restrictions.
        const uint32_t availability_wait = node.timeUntilUplink();
        if (availability_wait > 0U) {
            ESP_LOGI(TAG,
                     "uplink scheduler waiting %" PRIu32 " ms before TX",
                     availability_wait);
            wait_with_radio_sleep(radio, availability_wait);
        }

        ESP_LOGI(TAG,
                 "uplink attempt seq=%" PRIu32 " port=%u len=%u confirmed=%d",
                 sequence, (unsigned)LORAWAN_UPLINK_PORT,
                 (unsigned)uplink_length, LORAWAN_CONFIRMED_UPLINK);

        const uint32_t transmitted_fcnt = node.next_uplink_frame_counter();
        /*
         * sendReceive() covers TX, RX1, and RX2. Do not insert display, NVS,
         * or other shared-SPI work until it returns.
         */
        const int16_t send_state = node.sendReceive(
            uplink, uplink_length, LORAWAN_UPLINK_PORT,
            downlink, &downlink_length,
            LORAWAN_CONFIRMED_UPLINK != 0,
            &uplink_event, &downlink_event);

        const uint32_t next_fcnt = node.next_uplink_frame_counter();
        const bool frame_counter_advanced = next_fcnt != transmitted_fcnt;

        float downlink_rssi = 0.0f;
        float downlink_snr = 0.0f;
        if (send_state > RADIOLIB_ERR_NONE) {
            // Capture RX metrics before any following operation can touch radio.
            downlink_rssi = radio.getRSSI();
            downlink_snr = radio.getSNR();
        }

        // No frame was sent for UPLINK_UNAVAILABLE, so keep its sequence.
        if (send_state != RADIOLIB_ERR_UPLINK_UNAVAILABLE) {
            runtime.next_sequence = sequence + 1U;
        }
        /*
         * RadioLib may conservatively advance FCntUp even when TX or RX
         * returns an error. Persist every advance before any recovery path.
         */
        if (send_state >= RADIOLIB_ERR_NONE || frame_counter_advanced) {
            save_state_until_done(node, radio, true);
        }

        status.fcnt_up = node.getFCntUp();
        status.next_sequence = runtime.next_sequence;

        if (send_state >= RADIOLIB_ERR_NONE) {
            ++runtime.uplink_count;
            runtime.consecutive_failures = 0;
            status.uplink_count = runtime.uplink_count;
            status.tx_frequency_valid = true;
            status.tx_frequency_mhz = uplink_event.freq;
            record_uplink_channel(runtime, status, uplink_event.freq);

            ESP_LOGI(TAG,
                     "uplink complete state=%d RX=%s FCnt=%" PRIu32
                     " next-FCnt=%" PRIu32
                     " DR=%u freq=%.4fMHz power=%ddBm "
                     "total-up=%" PRIu32,
                     send_state,
                     send_state == 0 ? "none" :
                         (send_state == 1 ? "RX1" : "RX2"),
                     transmitted_fcnt, next_fcnt,
                     (unsigned)uplink_event.datarate,
                     (double)uplink_event.freq, uplink_event.power,
                     runtime.uplink_count);

            if (send_state > RADIOLIB_ERR_NONE) {
                // Positive states identify the RX window that delivered data.
                ++runtime.downlink_count;
                status.downlink_count = runtime.downlink_count;
                status.signal_valid = true;
                status.rssi = downlink_rssi;
                status.snr = downlink_snr;

                lorawan_log_downlink(downlink, downlink_length,
                                     downlink_event, send_state,
                                     downlink_rssi, downlink_snr);
                ESP_LOGI(TAG,
                         "session FCntUp=%" PRIu32
                         " NFCntDown=%" PRIu32
                         " AFCntDown=%" PRIu32
                         " total-down=%" PRIu32,
                         node.getFCntUp(), node.getNFCntDown(),
                         node.getAFCntDown(), runtime.downlink_count);

                char downlink_detail[24];
                snprintf(downlink_detail, sizeof(downlink_detail),
                         "RX%d FPort %u", send_state,
                         (unsigned)downlink_event.fPort);
                ui.update("DOWNLINK", downlink_detail, send_state);
            } else {
                ESP_LOGI(TAG, "Class A RX1/RX2 completed with no downlink");
                ui.update("UPLINK OK", "no downlink", send_state);
            }
        } else if (send_state == RADIOLIB_ERR_UPLINK_UNAVAILABLE) {
            ESP_LOGW(TAG,
                     "uplink deferred by scheduler: %d (%s)",
                     send_state, radiolib_state_name(send_state));
            ui.update("UPLINK DEFERRED",
                      radiolib_state_name(send_state), send_state);
        } else {
            ++runtime.consecutive_failures;
            ESP_LOGW(TAG,
                     "uplink failed: %d (%s), FCntUp=%" PRIu32
                     ", consecutive=%u",
                     send_state, radiolib_state_name(send_state),
                     node.getFCntUp(), runtime.consecutive_failures);

            // Rejoin only after explicit session loss or repeated failures.
            if (send_state == RADIOLIB_ERR_NETWORK_NOT_JOINED ||
                runtime.consecutive_failures >=
                    LORAWAN_SEND_FAILURES_BEFORE_REJOIN) {
                schedule_rejoin(node, radio, runtime, ui, send_state);
                continue;
            }
            ui.update("UPLINK ERROR",
                      radiolib_state_name(send_state), send_state);
        }

        // The longer of the user interval and legal wait controls the next TX.
        const uint32_t configured_wait =
            lorawan_seconds_to_ms(LORAWAN_UPLINK_INTERVAL_SECONDS);
        const uint32_t legal_wait = node.timeUntilUplink();
        const uint32_t wait_ms = std::max(configured_wait, legal_wait);
        ESP_LOGI(TAG,
                 "next uplink in %" PRIu32
                 " ms (configured=%" PRIu32 ", regulatory=%" PRIu32 ")",
                 wait_ms, configured_wait, legal_wait);
        wait_with_radio_sleep(radio, wait_ms);
    }
}
