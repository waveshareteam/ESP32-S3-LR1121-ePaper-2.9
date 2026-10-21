#include <inttypes.h>

#include <RadioLib.h>

#include "board_lorawan_radio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lorawan_flow.h"
#include "lorawan_profile.h"
#include "multicast_config.h"
#include "multicast_session.h"
#include "multicast_store.h"
#include "multicast_ui.h"
#include "radio_runtime.h"

/*
 * Top-level composition for example 13. Protocol details live in focused
 * modules so app_main() reads as the device lifecycle and recovery policy.
 */
namespace {

constexpr char kTag[] = "lorawan_multicast";
constexpr uint32_t kJoinBatchCooldownSeconds = 300U;
constexpr uint32_t kRadioInitRetrySeconds = 5U;
constexpr uint32_t kSessionRecoverySeconds = 30U;

void log_store_error(const char *operation,
                     const MulticastStoreResult &result)
{
    ESP_LOGE(kTag, "%s failed: store=%s esp=%s RadioLib=%d",
             operation,
             multicast_store_status_name(result.status),
             esp_err_to_name(result.esp_error),
             result.radiolib_status);
}

/* Retries board startup without allowing another task to access LR1121. */
LR1121 *initialize_radio(RadioRuntime &runtime)
{
    LR1121 *radio = nullptr;
    while (true) {
        if (!runtime.begin(RadioState::kRecovery)) {
            runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
        }
        const esp_err_t error = board_lorawan_radio_init(
            static_cast<float>(
                LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ) /
                1000000.0f,
            LORAWAN_INITIAL_POWER_DBM,
            &radio);
        runtime.end(RadioState::kIdle);
        if (error == ESP_OK && radio != nullptr) {
            return radio;
        }

        ESP_LOGE(kTag, "LR1121 initialization failed; retrying");
        runtime.update_ui("ERROR", "LR1121 init retry", error);
        delay_ms(seconds_to_ms(kRadioInitRetrySeconds));
    }
}

/* Restores nonces first; an invalid session never replaces valid nonce state. */
void restore_unicast_state(RadioRuntime &runtime,
                           SingleChannelLoRaWANNode &node)
{
    if (!runtime.begin(RadioState::kRecovery)) {
        runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
    }
    const UnicastRestoreReport restore =
        multicast_store_restore_unicast(node);
    runtime.end(RadioState::kIdle);

    if (restore.nonces.status != MulticastStoreStatus::kOk &&
        restore.nonces.status != MulticastStoreStatus::kNotFound) {
        log_store_error("restore OTAA nonce", restore.nonces);
        const bool discarded_cache =
            restore.nonces.status == MulticastStoreStatus::kRadioLibRejected &&
            restore.nonces.radiolib_status == RADIOLIB_ERR_NONCES_DISCARDED;
        if (!discarded_cache) {
            runtime.hold(
                "CONFIG REQUIRED", "nonce NVS invalid",
                static_cast<int32_t>(restore.nonces.status));
        }
        const MulticastStoreResult clear =
            multicast_store_clear_unicast_state();
        if (!clear.ok() && clear.status != MulticastStoreStatus::kNotFound) {
            log_store_error("clear discarded OTAA cache", clear);
            runtime.hold("ERROR", "OTAA CACHE CLEAR", clear.esp_error);
        }
        ESP_LOGW(kTag,
                 "discarded OTAA cache cleared; rejoining with current "
                 "JoinEUI");
        runtime.update_ui("OTAA CACHE RESET", "REJOINING", 0);
    }

    const bool first_otaa =
        restore.nonces.status == MulticastStoreStatus::kNotFound &&
        restore.session.status ==
            MulticastStoreStatus::kPrerequisiteFailed;
    if (first_otaa) {
        ESP_LOGI(kTag,
                 "no saved OTAA nonce; session restore skipped for first join");
    } else if (restore.session.status != MulticastStoreStatus::kOk &&
               restore.session.status != MulticastStoreStatus::kNotFound) {
        log_store_error("restore unicast session", restore.session);
        ESP_LOGW(kTag,
                 "invalid unicast session will be replaced by a new OTAA "
                 "session; nonce state remains authoritative");
    }
}

MulticastCounterState prepare_multicast_state(
    RadioRuntime &runtime,
    const MulticastStoreConfig &store_config)
{
    MulticastCounterState counter = {};
    const MulticastStoreResult result =
        multicast_store_prepare_multicast(store_config, &counter);
    if (!result.ok()) {
        invalid_multicast_state(runtime, result);
    }
    ESP_LOGI(kTag,
             "multicast NVS state %s; counter-valid=%d last=%" PRIu32,
             multicast_store_status_name(result.status),
             counter.counter_valid,
             counter.last_fcnt_down);
    return counter;
}

}  // namespace

extern "C" void app_main(void)
{
    RadioRuntime runtime;
    runtime.ui().multicast_address = MULTICAST_MC_ADDR;
    runtime.ui().group_id = MULTICAST_GROUP_ID;
    runtime.ui().join_eui = LORAWAN_JOIN_EUI;

    /* The display worker is optional; radio operation does not depend on it. */
    const esp_err_t ui_error = multicast_ui_start();
    if (ui_error != ESP_OK) {
        ESP_LOGW(kTag, "e-paper worker unavailable: %s",
                 esp_err_to_name(ui_error));
    }
    if (!runtime.initialize()) {
        runtime.hold("ERROR", "radio sync init", ESP_ERR_NO_MEM);
    }

    ESP_LOGI(kTag,
             "RadioLib 7.7.1 static multicast example: "
             "OTAA unicast Class A + multicast Class C");
    ESP_LOGI(kTag,
             "unicast EU868 single-channel=%" PRIu32 "Hz DR%u ADR=off "
             "sub-band=%u; multicast slot=%u freq=%" PRIu32 "Hz DR%u",
             static_cast<uint32_t>(
                 LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ),
             static_cast<unsigned>(LORAWAN_SINGLE_CHANNEL_DR),
             static_cast<unsigned>(LORAWAN_SUB_BAND),
             static_cast<unsigned>(MULTICAST_GROUP_ID),
             static_cast<uint32_t>(MULTICAST_FREQUENCY_HZ),
             static_cast<unsigned>(MULTICAST_DATARATE));
    ESP_LOGI(kTag,
             "LR1121 frequency-error reporting is unavailable in "
             "RadioLib 7.7.1; only RSSI and SNR are reported");

    ensure_store_open(runtime);

#if MULTICAST_CLEAR_STATE_ON_BOOT
    /* This maintenance action must be synchronized with server McFCntDown. */
    const MulticastStoreResult clear_result =
        multicast_store_clear_multicast();
    if (!clear_result.ok() &&
        clear_result.status != MulticastStoreStatus::kNotFound) {
        invalid_multicast_state(runtime, clear_result);
    }
    ESP_LOGW(kTag,
             "multicast NVS state explicitly cleared; reset/synchronize "
             "server McFCntDown before disabling the maintenance switch");
    runtime.hold("CONFIG REQUIRED", "sync server counter", 0);
#endif

    /* Reject placeholders before the first possible radio transmission. */
    if (!lorawan_profile_configuration_is_valid()) {
        runtime.hold(
            "CONFIG REQUIRED", "edit secrets", ESP_ERR_INVALID_STATE);
    }

    const MulticastStoreConfig store_config = {
        .group_id = MULTICAST_GROUP_ID,
        .config_generation = MULTICAST_CONFIG_GENERATION,
        .mc_addr = MULTICAST_MC_ADDR,
        .frequency_hz = MULTICAST_FREQUENCY_HZ,
        .datarate = MULTICAST_DATARATE,
    };
    MulticastCounterState multicast_counter =
        prepare_multicast_state(runtime, store_config);

    LR1121 *radio = initialize_radio(runtime);
    SingleChannelLoRaWANNode node(
        radio, lorawan_profile_region(), LORAWAN_SUB_BAND);

    if (!runtime.begin(RadioState::kRecovery)) {
        runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
    }
    const int16_t begin_state = lorawan_profile_begin_otaa(node);
    runtime.end(RadioState::kIdle);
    if (begin_state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(kTag, "beginOTAA failed: RadioLib %d", begin_state);
        runtime.hold("ERROR", "begin OTAA", begin_state);
    }
    restore_unicast_state(runtime, node);

    bool session_restored = false;
    bool multicast_active = false;
    while (true) {
        /* Keep the verified unicast activation independent of multicast. */
        while (!node.isActivated()) {
            if (lorawan_activate_unicast(
                    runtime, node, multicast_counter,
                    &session_restored)) {
                break;
            }
            ESP_LOGW(kTag,
                     "OTAA attempt batch exhausted; Class C has not started");
            runtime.update_ui("ERROR", "join cooldown", 0);
            delay_ms(seconds_to_ms(kJoinBatchCooldownSeconds));
        }

        if (!restart_multicast_until_ready(
                runtime, node, multicast_counter,
                &multicast_active)) {
            continue;
        }

        const LoRaWANLoopDecision decision = lorawan_run_active_loop(
            runtime, node, *radio, store_config,
            &multicast_counter, &multicast_active);
        if (decision != LoRaWANLoopDecision::kRejoin) {
            continue;
        }

        /* Retain multicast FCntDown while rebuilding only the unicast session. */
        ESP_LOGW(kTag,
                 "unicast session recovery requested; multicast reception "
                 "is paused and group counter state is retained");
        stop_multicast(runtime, node, &multicast_active);
        if (!runtime.begin(RadioState::kRecovery)) {
            runtime.hold("ERROR", "radio mutex", ESP_ERR_INVALID_STATE);
        }
        node.clearSession();
        runtime.end(RadioState::kIdle);
        if (!persist_unicast_with_pause(
                runtime, node, true, multicast_counter, nullptr)) {
            runtime.hold(
                "ERROR", "clear session NVS", ESP_ERR_INVALID_STATE);
        }
        session_restored = false;
        runtime.ui().session_restored = false;
        runtime.update_ui("ERROR", "unicast rejoin", 0);
        delay_ms(seconds_to_ms(kSessionRecoverySeconds));
    }
}
