#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include <RadioLib.h>

#include "abp_config.h"
#include "abp_node.h"
#include "abp_state.h"
#include "abp_ui.h"
#include "board_lorawan_radio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr size_t kSessionKeyLength = 16U;
constexpr size_t kUplinkPayloadLength = 15U;
constexpr size_t kSafeTextLimit = 64U;
constexpr uint32_t kMillisecondsPerSecond = 1000U;
const char *const kTag = "lorawan_abp";

const uint8_t kAppSKey[kSessionKeyLength] = LORAWAN_ABP_APP_S_KEY_BYTES;
const uint8_t kNwkSKey[kSessionKeyLength] = LORAWAN_ABP_NWK_S_KEY_BYTES;
const uint8_t kFNwkSIntKey[kSessionKeyLength] =
    LORAWAN_ABP_F_NWK_S_INT_KEY_BYTES;
const uint8_t kSNwkSIntKey[kSessionKeyLength] =
    LORAWAN_ABP_S_NWK_S_INT_KEY_BYTES;
const uint8_t kNwkSEncKey[kSessionKeyLength] =
    LORAWAN_ABP_NWK_S_ENC_KEY_BYTES;

void delay_ms(uint32_t duration_ms)
{
    while (duration_ms > 0U) {
        const uint32_t chunk = std::min(duration_ms, UINT32_C(60000));
        vTaskDelay(pdMS_TO_TICKS(chunk));
        duration_ms -= chunk;
    }
}

[[noreturn]] void halt_safely()
{
    while (true) {
        delay_ms(UINT32_C(60000));
    }
}

bool key_is_uniform(const uint8_t *key, uint8_t value)
{
    for (size_t i = 0; i < kSessionKeyLength; ++i) {
        if (key[i] != value) {
            return false;
        }
    }
    return true;
}

bool key_is_placeholder(const uint8_t *key)
{
    return key_is_uniform(key, 0x00U) || key_is_uniform(key, 0xFFU);
}

bool validate_session_key(const char *name, const uint8_t *key)
{
    if (!key_is_placeholder(key)) {
        return true;
    }
    ESP_LOGE(kTag, "%s is missing or still a placeholder", name);
    return false;
}

bool credentials_are_valid()
{
    bool valid = true;
    if (LORAWAN_ABP_DEV_ADDR == 0U ||
        LORAWAN_ABP_DEV_ADDR == UINT32_MAX) {
        ESP_LOGE(kTag, "DevAddr is missing or still a placeholder");
        valid = false;
    }
    valid = validate_session_key("AppSKey", kAppSKey) && valid;

#if LORAWAN_ABP_MAC_VERSION == LORAWAN_ABP_MAC_VERSION_1_0
    valid = validate_session_key("NwkSKey", kNwkSKey) && valid;
#else
    valid = validate_session_key("FNwkSIntKey", kFNwkSIntKey) && valid;
    valid = validate_session_key("SNwkSIntKey", kSNwkSIntKey) && valid;
    valid = validate_session_key("NwkSEncKey", kNwkSEncKey) && valid;
#endif
    return valid;
}

LoRaWANBand_t make_configured_band()
{
    LoRaWANBand_t band = LORAWAN_ABP_REGION_OBJECT;
    const uint32_t frequency_100_hz =
        LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ / 100U;

    band.txFreqs[0].idx = 0;
    band.txFreqs[0].freq = frequency_100_hz;
    band.txFreqs[0].drMin = LORAWAN_ABP_SINGLE_CHANNEL_DR;
    band.txFreqs[0].drMax = LORAWAN_ABP_SINGLE_CHANNEL_DR;
    band.txFreqs[0].dr = LORAWAN_ABP_SINGLE_CHANNEL_DR;
    band.txFreqs[1] = {};
    band.txFreqs[2] = {};
    return band;
}

const LoRaWANBand_t &configured_band()
{
    static const LoRaWANBand_t band = make_configured_band();
    return band;
}

uint8_t configured_mac_revision()
{
    return LORAWAN_ABP_MAC_VERSION == LORAWAN_ABP_MAC_VERSION_1_1
               ? 1U
               : 0U;
}

uint32_t configuration_fingerprint()
{
    uint32_t crc = 0;
    const auto append = [&crc](const void *data, size_t length) {
        crc = esp_rom_crc32_le(
            crc, static_cast<const uint8_t *>(data),
            static_cast<uint32_t>(length));
    };

    const uint32_t dev_addr = LORAWAN_ABP_DEV_ADDR;
    const uint8_t mac_revision = configured_mac_revision();
    const uint32_t radio_version = RADIOLIB_VERSION;
    const uint32_t frequency = LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ;
    const uint8_t data_rate = LORAWAN_ABP_SINGLE_CHANNEL_DR;
    const uint8_t sub_band = LORAWAN_ABP_SUB_BAND;
    const int8_t tx_power = LORAWAN_ABP_TX_POWER_DBM;
    const uint8_t adr = LORAWAN_ABP_ADR_ENABLED;

    append(&dev_addr, sizeof(dev_addr));
    append(&mac_revision, sizeof(mac_revision));
    append(&radio_version, sizeof(radio_version));
    append(LORAWAN_ABP_REGION_NAME, std::strlen(LORAWAN_ABP_REGION_NAME));
    append(&frequency, sizeof(frequency));
    append(&data_rate, sizeof(data_rate));
    append(&sub_band, sizeof(sub_band));
    append(&tx_power, sizeof(tx_power));
    append(&adr, sizeof(adr));
    append(kAppSKey, sizeof(kAppSKey));
#if LORAWAN_ABP_MAC_VERSION == LORAWAN_ABP_MAC_VERSION_1_0
    append(kNwkSKey, sizeof(kNwkSKey));
#else
    append(kFNwkSIntKey, sizeof(kFNwkSIntKey));
    append(kSNwkSIntKey, sizeof(kSNwkSIntKey));
    append(kNwkSEncKey, sizeof(kNwkSEncKey));
#endif
    return crc;
}

uint32_t doubled_backoff(uint32_t current, uint32_t maximum)
{
    if (current >= maximum / 2U) {
        return maximum;
    }
    return std::min(current * 2U, maximum);
}

void storage_begin_or_wait(abp_state::Store &store, AbpStatusUi &ui)
{
    uint32_t backoff = LORAWAN_ABP_NVS_RETRY_BASE_SECONDS;
    while (true) {
        const esp_err_t error = store.begin();
        if (error == ESP_OK) {
            return;
        }
        ESP_LOGE(kTag, "NVS initialization failed: %s; retry in %" PRIu32 " s",
                 esp_err_to_name(error), backoff);
        ui.error("NVS INIT", error);
        delay_ms(backoff * kMillisecondsPerSecond);
        backoff = doubled_backoff(
            backoff, LORAWAN_ABP_NVS_RETRY_MAX_SECONDS);
    }
}

abp_state::LoadResult storage_load_or_wait(
    abp_state::Store &store,
    const abp_state::Identity &identity,
    abp_state::Snapshot &snapshot,
    AbpStatusUi &ui)
{
    uint32_t backoff = LORAWAN_ABP_NVS_RETRY_BASE_SECONDS;
    while (true) {
        const abp_state::LoadResult result =
            store.load(identity, &snapshot);
        if (result.status != abp_state::LoadStatus::StorageError) {
            return result;
        }
        ESP_LOGE(kTag, "NVS load failed: %s; retry in %" PRIu32 " s",
                 esp_err_to_name(result.error), backoff);
        ui.error("NVS LOAD", result.error);
        delay_ms(backoff * kMillisecondsPerSecond);
        backoff = doubled_backoff(
            backoff, LORAWAN_ABP_NVS_RETRY_MAX_SECONDS);
    }
}

[[noreturn]] void clear_state_and_halt(abp_state::Store &store,
                                       AbpStatusUi &ui)
{
    uint32_t backoff = LORAWAN_ABP_NVS_RETRY_BASE_SECONDS;
    while (true) {
        const esp_err_t error = store.clear();
        if (error == ESP_OK) {
            ESP_LOGW(kTag,
                     "ABP state namespace cleared; radio remains disabled");
            ui.update("ABP", "STATE CLEARED", ESP_OK);
            halt_safely();
        }
        ESP_LOGE(kTag, "ABP state clear failed: %s; retry in %" PRIu32 " s",
                 esp_err_to_name(error), backoff);
        ui.error("NVS CLEAR", error);
        delay_ms(backoff * kMillisecondsPerSecond);
        backoff = doubled_backoff(
            backoff, LORAWAN_ABP_NVS_RETRY_MAX_SECONDS);
    }
}

LR1121 *initialize_radio_or_wait(AbpStatusUi &ui)
{
    uint32_t backoff = LORAWAN_ABP_RADIO_RETRY_BASE_SECONDS;
    while (true) {
        LR1121 *radio = nullptr;
        const esp_err_t error = board_lorawan_radio_init(&radio);
        if (error == ESP_OK && radio != nullptr) {
            return radio;
        }
        ESP_LOGE(kTag, "LR1121 initialization failed; retry in %" PRIu32 " s",
                 backoff);
        ui.error("LR1121 INIT", error == ESP_OK ? ESP_FAIL : error);
        delay_ms(backoff * kMillisecondsPerSecond);
        backoff = doubled_backoff(
            backoff, LORAWAN_ABP_ERROR_BACKOFF_MAX_SECONDS);
    }
}

int16_t begin_abp(PersistentAbpNode &node)
{
#if LORAWAN_ABP_MAC_VERSION == LORAWAN_ABP_MAC_VERSION_1_0
    return node.beginABP(LORAWAN_ABP_DEV_ADDR,
                         nullptr, nullptr, kNwkSKey, kAppSKey);
#else
    return node.beginABP(LORAWAN_ABP_DEV_ADDR,
                         kFNwkSIntKey, kSNwkSIntKey,
                         kNwkSEncKey, kAppSKey);
#endif
}

bool activation_succeeded(int16_t state)
{
    return state == RADIOLIB_ERR_NONE ||
           state == RADIOLIB_LORAWAN_NEW_SESSION ||
           state == RADIOLIB_LORAWAN_SESSION_RESTORED;
}

struct RadioStateLabel {
    int16_t code;
    const char *label;
};

constexpr RadioStateLabel kRadioStateLabels[] = {
    {RADIOLIB_ERR_NONE, "none"},
    {RADIOLIB_ERR_CHIP_NOT_FOUND, "chip-not-found"},
    {RADIOLIB_ERR_PACKET_TOO_LONG, "packet-too-long"},
    {RADIOLIB_ERR_RX_TIMEOUT, "rx-timeout"},
    {RADIOLIB_ERR_MIC_MISMATCH, "mic-mismatch"},
    {RADIOLIB_ERR_INVALID_FREQUENCY, "invalid-frequency"},
    {RADIOLIB_ERR_INVALID_OUTPUT_POWER, "invalid-output-power"},
    {RADIOLIB_ERR_NETWORK_NOT_JOINED, "session-not-active"},
    {RADIOLIB_ERR_DOWNLINK_MALFORMED, "downlink-malformed"},
    {RADIOLIB_ERR_INVALID_PORT, "invalid-port"},
    {RADIOLIB_ERR_NO_RX_WINDOW, "no-rx-window"},
    {RADIOLIB_ERR_UPLINK_UNAVAILABLE, "uplink-unavailable"},
    {RADIOLIB_ERR_DWELL_TIME_EXCEEDED, "dwell-time-exceeded"},
    {RADIOLIB_ERR_CHECKSUM_MISMATCH, "checksum-mismatch"},
    {RADIOLIB_LORAWAN_SESSION_RESTORED, "session-restored"},
    {RADIOLIB_LORAWAN_NEW_SESSION, "new-session"},
    {RADIOLIB_ERR_NONCES_DISCARDED, "nonces-discarded"},
    {RADIOLIB_ERR_SESSION_DISCARDED, "session-discarded"},
    {ABP_ERR_FCNT_EXHAUSTED, "fcnt-exhausted"},
};

const char *radio_state_name(int16_t state)
{
    for (const RadioStateLabel &entry : kRadioStateLabels) {
        if (entry.code == state) {
            return entry.label;
        }
    }
    return "other";
}

void require_radio_success(int16_t state,
                           const char *operation,
                           const char *ui_detail,
                           AbpStatusUi &ui)
{
    if (state == RADIOLIB_ERR_NONE) {
        return;
    }
    ESP_LOGE(kTag, "%s failed: %d (%s)",
             operation, state, radio_state_name(state));
    ui.error(ui_detail, state);
    halt_safely();
}

void refresh_counters(PersistentAbpNode &node,
                      AbpStatusUi &ui,
                      uint32_t application_sequence)
{
    node.refresh_frame_counters();
    abp_ui_state_t &status = ui.state();
    status.dev_addr = LORAWAN_ABP_DEV_ADDR;
    status.application_sequence = application_sequence;
}

void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value >> 24);
    output[1] = static_cast<uint8_t>(value >> 16);
    output[2] = static_cast<uint8_t>(value >> 8);
    output[3] = static_cast<uint8_t>(value);
}

void build_uplink(uint8_t *payload,
                  uint32_t sequence,
                  bool restored_session)
{
    payload[0] = 1U;
    payload[1] =
        (LORAWAN_ABP_CONFIRMED_UPLINK ? 1U : 0U) |
        (LORAWAN_ABP_ADR_ENABLED ? 2U : 0U) |
        (restored_session ? 4U : 0U);
    write_u32_be(&payload[2], sequence);
    write_u32_be(
        &payload[6],
        static_cast<uint32_t>(esp_timer_get_time() / UINT64_C(1000000)));
    write_u32_be(&payload[10], esp_get_free_heap_size());
    payload[14] = static_cast<uint8_t>(esp_reset_reason());
}

void log_downlink(const uint8_t *data,
                  size_t length,
                  int16_t rx_window,
                  const LoRaWANEvent_t &event,
                  float rssi,
                  float snr)
{
    ESP_LOGI(kTag,
             "downlink RX%d port=%u length=%zu FCnt=%" PRIu32
             " confirmed=%d ACK=%d RSSI=%.1f dBm SNR=%.1f dB",
             rx_window, static_cast<unsigned>(event.fPort), length,
             event.fCnt, event.confirmed, event.confirming,
             static_cast<double>(rssi), static_cast<double>(snr));
    if (length == 0U) {
        return;
    }

    ESP_LOGI(kTag, "downlink HEX:");
    ESP_LOG_BUFFER_HEX(kTag, data, length);

    char safe_text[kSafeTextLimit + 1U] = {};
    const size_t shown = std::min(length, kSafeTextLimit);
    for (size_t i = 0; i < shown; ++i) {
        safe_text[i] =
            data[i] >= 0x20U && data[i] <= 0x7EU
                ? static_cast<char>(data[i])
                : '.';
    }
    safe_text[shown] = '\0';
    ESP_LOGI(kTag, "downlink safe text: %s%s",
             safe_text, length > shown ? "..." : "");
}

uint32_t radio_error_backoff(unsigned consecutive_failures)
{
    uint32_t backoff = LORAWAN_ABP_ERROR_BACKOFF_BASE_SECONDS;
    for (unsigned i = 1U; i < consecutive_failures; ++i) {
        backoff = doubled_backoff(
            backoff, LORAWAN_ABP_ERROR_BACKOFF_MAX_SECONDS);
    }
    const uint32_t jitter_range = backoff / 4U + 1U;
    return std::min(
        backoff + esp_random() % jitter_range,
        static_cast<uint32_t>(LORAWAN_ABP_ERROR_BACKOFF_MAX_SECONDS));
}

void log_configuration()
{
    ESP_LOGI(kTag, "LR1121 LoRaWAN ABP, RadioLib %u.%u.%u",
             static_cast<unsigned>(RADIOLIB_VERSION_MAJOR),
             static_cast<unsigned>(RADIOLIB_VERSION_MINOR),
             static_cast<unsigned>(RADIOLIB_VERSION_PATCH));
    ESP_LOGI(kTag,
             "ABP LoRaWAN 1.%u region=%s sub-band=%u "
             "single-channel=%" PRIu32 " Hz DR%u TX=%d dBm "
             "port=%u interval=%" PRIu32 " s ADR=%d confirmed=%d",
             configured_mac_revision(),
             LORAWAN_ABP_REGION_NAME,
             static_cast<unsigned>(LORAWAN_ABP_SUB_BAND),
             static_cast<uint32_t>(
                 LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ),
             static_cast<unsigned>(LORAWAN_ABP_SINGLE_CHANNEL_DR),
             LORAWAN_ABP_TX_POWER_DBM,
             static_cast<unsigned>(LORAWAN_ABP_UPLINK_PORT),
             static_cast<uint32_t>(LORAWAN_ABP_UPLINK_INTERVAL_SECONDS),
             LORAWAN_ABP_ADR_ENABLED,
             LORAWAN_ABP_CONFIRMED_UPLINK);
    ESP_LOGI(kTag,
             "ABP credentials are never printed; activation sends no join frames");
}

}  // namespace

extern "C" void app_main(void)
{
    static_assert(kUplinkPayloadLength == 15U,
                  "The documented ABP payload must stay 15 bytes");

    AbpStatusUi ui;
    ui.state().dev_addr = LORAWAN_ABP_DEV_ADDR;
    log_configuration();

    abp_state::Store store;
    if (LORAWAN_ABP_CLEAR_STATE_CONFIRM ==
        LORAWAN_ABP_CLEAR_STATE_MAGIC) {
        ui.update("ABP", "CLEARING STATE", ESP_OK);
        storage_begin_or_wait(store, ui);
        clear_state_and_halt(store, ui);
    }

    if (!credentials_are_valid()) {
        ESP_LOGE(kTag,
                 "configure main/abp_credentials.h; radio transmission disabled");
        ui.update("CONFIG REQUIRED", "EDIT CREDENTIALS",
                  ESP_ERR_INVALID_STATE);
        halt_safely();
    }

    ui.update("ABP", "LOADING STATE", ESP_OK);
    storage_begin_or_wait(store, ui);

    // Resolve persistent state before radio setup so invalid counters can never
    // reach the air.
    const abp_state::Identity identity = {
        LORAWAN_ABP_DEV_ADDR,
        configured_mac_revision(),
        configuration_fingerprint(),
    };
    abp_state::Snapshot snapshot = {};
    const abp_state::LoadResult loaded =
        storage_load_or_wait(store, identity, snapshot, ui);
    ESP_LOGI(kTag, "ABP NVS state: %s",
             abp_state::load_status_name(loaded.status));

    if (loaded.status == abp_state::LoadStatus::Invalid) {
        ESP_LOGE(kTag,
                 "ABP state is corrupt or incompatible; refusing to reset counters");
        ui.error("NVS STATE INVALID", ESP_ERR_INVALID_CRC);
        halt_safely();
    }
    if (loaded.status != abp_state::LoadStatus::Restored &&
        loaded.status != abp_state::LoadStatus::NotFound) {
        ui.error("NVS STATE", loaded.error);
        halt_safely();
    }
    if (loaded.status == abp_state::LoadStatus::NotFound) {
        snapshot.app_sequence = 1U;
    }
    if (loaded.status == abp_state::LoadStatus::Restored &&
        (snapshot.flags & abp_state::kFlagRadioTransactionPending) != 0U) {
        ESP_LOGE(kTag,
                 "previous Class A transaction did not reach its final NVS "
                 "commit; downlink counters are ambiguous, radio disabled");
        ui.error("SESSION AMBIGUOUS", ESP_ERR_INVALID_STATE);
        halt_safely();
    }

    LR1121 *radio = initialize_radio_or_wait(ui);
    const LoRaWANBand_t &band = configured_band();
    PersistentAbpNode node(
        radio, &band, LORAWAN_ABP_SUB_BAND,
        store, identity, snapshot, ui);

    // beginABP installs credentials without transmitting. For a restored
    // session, load both RadioLib buffers before activateABP().
    int16_t state = begin_abp(node);
    require_radio_success(state, "beginABP", "BEGIN ABP", ui);

    const bool restoring =
        loaded.status == abp_state::LoadStatus::Restored;
    if (restoring) {
        state = node.setBufferNonces(snapshot.nonces);
        require_radio_success(
            state, "setBufferNonces", "NONCES REJECTED", ui);
        state = node.setBufferSession(snapshot.session);
        require_radio_success(
            state, "setBufferSession", "SESSION REJECTED", ui);
    }

    state = node.activateABP();
    if (!activation_succeeded(state) || !node.isActivated()) {
        ESP_LOGE(kTag, "ABP activation failed: %d (%s)",
                 state, radio_state_name(state));
        ui.error("ABP ACTIVATE", state);
        halt_safely();
    }

    state = node.apply_network_policy();
    require_radio_success(state, "apply_network_policy", "RADIO POLICY", ui);

    node.persist_current_or_wait();
    refresh_counters(node, ui, snapshot.app_sequence);
    ui.update("ABP ACTIVE",
              restoring ? "SESSION RESTORED" : "SESSION ACTIVATED", state);
    ESP_LOGI(kTag,
             "ABP session %s: DevAddr=%08" PRIX32
             " next-FCntUp=%" PRIu32,
             restoring ? "restored" : "activated",
             static_cast<uint32_t>(LORAWAN_ABP_DEV_ADDR),
             node.next_fcnt_up());

    unsigned consecutive_failures = 0U;
    while (true) {
        // Reapply the fixed channel after every Class A exchange because
        // network MAC commands may update RadioLib's channel state.
        state = node.apply_network_policy();
        if (state != RADIOLIB_ERR_NONE) {
            ++consecutive_failures;
            const uint32_t backoff =
                radio_error_backoff(consecutive_failures);
            ESP_LOGE(kTag,
                     "radio policy error %d (%s); retry in %" PRIu32 " s",
                     state, radio_state_name(state), backoff);
            ui.error("RADIO POLICY", state);
            delay_ms(backoff * kMillisecondsPerSecond);
            continue;
        }

        if (snapshot.app_sequence == UINT32_MAX) {
            ESP_LOGE(kTag,
                     "application sequence exhausted; radio transmission stopped");
            ui.error("APP SEQ EXHAUSTED", ESP_ERR_INVALID_SIZE);
            halt_safely();
        }

        const uint32_t sequence = snapshot.app_sequence;
        snapshot.app_sequence += 1U;
        uint8_t uplink[kUplinkPayloadLength] = {};
        build_uplink(uplink, sequence, restoring);

        uint8_t downlink[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE + 1U] = {};
        size_t downlink_length = sizeof(downlink);
        LoRaWANEvent_t event_up = {};
        LoRaWANEvent_t event_down = {};

        abp_ui_state_t &status = ui.state();
        status.rx_valid = false;
        status.signal_valid = false;

        ESP_LOGI(kTag,
                 "uplink sequence=%" PRIu32
                 " port=%u length=%zu next-FCntUp=%" PRIu32
                 " confirmed=%d",
                 sequence,
                 static_cast<unsigned>(LORAWAN_ABP_UPLINK_PORT),
                 sizeof(uplink), node.next_fcnt_up(),
                 LORAWAN_ABP_CONFIRMED_UPLINK);

        state = node.sendReceive(
            uplink, sizeof(uplink), LORAWAN_ABP_UPLINK_PORT,
            downlink, &downlink_length,
            LORAWAN_ABP_CONFIRMED_UPLINK != 0,
            &event_up, &event_down);

        refresh_counters(node, ui, sequence);
        status.tx_valid = state >= RADIOLIB_ERR_NONE;
        status.tx_port = LORAWAN_ABP_UPLINK_PORT;
        status.tx_length = sizeof(uplink);
        bool retry_with_backoff = false;
        const auto mark_failure = [&]() {
            ++consecutive_failures;
            retry_with_backoff = true;
        };

        if (state > 0) {
            const bool ack_received =
                LORAWAN_ABP_CONFIRMED_UPLINK == 0 ||
                event_down.confirming;
            if (ack_received) {
                consecutive_failures = 0U;
            } else {
                mark_failure();
                ESP_LOGW(kTag,
                         "downlink received without ACK for confirmed uplink");
            }
            const float rssi = radio->getRSSI();
            const float snr = radio->getSNR();
            status.rx_valid = true;
            status.rx_window = state;
            status.rx_port = event_down.fPort;
            status.rx_length = downlink_length;
            status.signal_valid = true;
            status.rssi_dbm = rssi;
            status.snr_db = snr;
            log_downlink(
                downlink, downlink_length, state, event_down, rssi, snr);

            char detail[32] = {};
            if (LORAWAN_ABP_CONFIRMED_UPLINK != 0) {
                std::snprintf(
                    detail, sizeof(detail), "%s RX%d P%u %zuB",
                    event_down.confirming ? "ACK" : "NO ACK",
                    state, static_cast<unsigned>(event_down.fPort),
                    downlink_length);
            } else {
                std::snprintf(
                    detail, sizeof(detail), "RX%d P%u %zuB",
                    state, static_cast<unsigned>(event_down.fPort),
                    downlink_length);
            }
            if (ack_received) {
                ui.update("ABP ACTIVE", detail, state);
            } else {
                ui.error(detail, ESP_ERR_TIMEOUT);
            }
        } else if (state == RADIOLIB_ERR_NONE) {
            if (LORAWAN_ABP_CONFIRMED_UPLINK != 0) {
                mark_failure();
                ESP_LOGW(kTag,
                         "confirmed uplink completed without an ACK");
                ui.error("TX NO ACK", ESP_ERR_TIMEOUT);
            } else {
                consecutive_failures = 0U;
                ESP_LOGI(kTag, "uplink complete; no Class A downlink");
                ui.update("ABP ACTIVE", "TX OK NO RX", state);
            }
        } else {
            mark_failure();
            ESP_LOGE(kTag, "sendReceive failed: %d (%s)",
                     state, radio_state_name(state));
            ui.error(radio_state_name(state), state);
            if (state == ABP_ERR_FCNT_EXHAUSTED) {
                halt_safely();
            }
        }

        uint32_t wait_ms =
            LORAWAN_ABP_UPLINK_INTERVAL_SECONDS *
            kMillisecondsPerSecond;
        if (retry_with_backoff) {
            const uint32_t error_wait =
                radio_error_backoff(consecutive_failures) *
                kMillisecondsPerSecond;
            wait_ms = std::max(wait_ms, error_wait);
        }
        wait_ms = std::max(wait_ms, node.timeUntilUplink());
        ESP_LOGI(kTag, "next uplink in %" PRIu32 " ms", wait_ms);
        delay_ms(wait_ms);
    }
}
