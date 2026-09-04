#include "multicast_session.h"

#include <algorithm>
#include <inttypes.h>

#include "esp_log.h"
#include "multicast_config.h"

/*
 * Class C session lifecycle and persistence recovery. This module coordinates
 * RadioLib state with NVS without mixing multicast and unicast sessions.
 */
namespace {

constexpr char kTag[] = "lorawan_multicast";
constexpr uint32_t kStoreRetrySeconds = 5U;

bool store_result_retryable(const MulticastStoreResult &result)
{
    return result.status == MulticastStoreStatus::kNvsError ||
           result.status == MulticastStoreStatus::kNotInitialized ||
           result.status == MulticastStoreStatus::kLockError;
}

void log_store_error(const char *operation,
                     const MulticastStoreResult &result)
{
    ESP_LOGE(kTag, "%s failed: store=%s esp=%s RadioLib=%d",
             operation,
             multicast_store_status_name(result.status),
             esp_err_to_name(result.esp_error),
             result.radiolib_status);
}

uint32_t retry_backoff_seconds(unsigned attempt)
{
    uint32_t value = MULTICAST_ERROR_BACKOFF_SECONDS;
    for (unsigned i = 1U; i < attempt; ++i) {
        if (value >= MULTICAST_MAX_BACKOFF_SECONDS / 2U) {
            return MULTICAST_MAX_BACKOFF_SECONDS;
        }
        value *= 2U;
    }
    return std::min(value,
                    static_cast<uint32_t>(MULTICAST_MAX_BACKOFF_SECONDS));
}

/* Deterministic configuration errors require operator action, not retries. */
bool multicast_configuration_error(int16_t state)
{
    switch (state) {
    case RADIOLIB_ERR_INVALID_MULTICAST_GROUP:
    case RADIOLIB_ERR_UNSUPPORTED:
    case RADIOLIB_ERR_INVALID_FREQUENCY:
    case RADIOLIB_ERR_INVALID_DATA_RATE:
    case RADIOLIB_ERR_MULTICAST_FCNT_INVALID:
    case RADIOLIB_ERR_NULL_POINTER:
        return true;
    default:
        return false;
    }
}

int16_t start_multicast_once(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastCounterState &counter,
    bool *active)
{
    if (counter.counter_valid &&
        counter.last_fcnt_down >= MULTICAST_MC_FCNT_MAX) {
        return RADIOLIB_ERR_MULTICAST_FCNT_INVALID;
    }

    MulticastGroup_t group = {};
    lorawan_profile_configure_multicast_group(
        group, counter.counter_valid ? counter.last_fcnt_down : 0U);

    if (!runtime.begin(RadioState::kRecovery)) {
        return RADIOLIB_ERR_UNKNOWN;
    }
    const int16_t state =
        node.startMulticastSession(MULTICAST_GROUP_ID, &group);
    const bool class_c_ready =
        state == RADIOLIB_ERR_NONE &&
        node.getMulticastClass() == RADIOLIB_LORAWAN_CLASS_C;
    runtime.end(class_c_ready ? RadioState::kClassC : RadioState::kIdle);

    *active = class_c_ready;
    if (state == RADIOLIB_ERR_NONE && !class_c_ready) {
        return RADIOLIB_ERR_UNSUPPORTED;
    }
    return state;
}

MulticastStoreResult save_unicast_once(
    RadioRuntime &runtime,
    LoRaWANNode &node,
    bool include_session,
    bool class_c_active)
{
    if (!runtime.begin(RadioState::kRecovery)) {
        return {
            MulticastStoreStatus::kLockError,
            ESP_ERR_INVALID_STATE,
            RADIOLIB_ERR_UNKNOWN,
        };
    }
    const MulticastStoreResult result =
        include_session
            ? multicast_store_save_unicast_state(node)
            : multicast_store_save_unicast_nonces(node);
    runtime.end(class_c_active ? RadioState::kClassC : RadioState::kIdle);
    return result;
}

}  // namespace

void ensure_store_open(RadioRuntime &runtime)
{
    while (true) {
        const MulticastStoreResult result = multicast_store_init();
        if (result.ok()) {
            return;
        }
        log_store_error("NVS initialization", result);
        runtime.update_ui("ERROR", "NVS init retry", result.esp_error);
        delay_ms(seconds_to_ms(kStoreRetrySeconds));
    }
}

[[noreturn]] void invalid_multicast_state(
    RadioRuntime &runtime,
    const MulticastStoreResult &result)
{
    log_store_error("multicast state validation", result);
    ESP_LOGE(kTag,
             "multicast is disabled; clear state explicitly and synchronize "
             "the server McFCntDown before retrying");
    runtime.hold("CONFIG REQUIRED", "multicast NVS invalid",
                 static_cast<int32_t>(result.status));
}

bool stop_multicast(RadioRuntime &runtime,
                    SingleChannelLoRaWANNode &node,
                    bool *active)
{
    if (active == nullptr) {
        return false;
    }
    if (!*active) {
        return true;
    }
    if (!runtime.begin(RadioState::kRecovery)) {
        return false;
    }
    const bool stopped =
        node.stopMulticastSession(MULTICAST_GROUP_ID, false);
    runtime.end(stopped ? RadioState::kIdle : RadioState::kClassC);
    if (!stopped) {
        ESP_LOGW(kTag, "RadioLib did not stop multicast slot %u",
                 static_cast<unsigned>(MULTICAST_GROUP_ID));
        return false;
    }
    *active = false;
    return true;
}

bool restart_multicast_until_ready(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastCounterState &counter,
    bool *active)
{
    stop_multicast(runtime, node, active);
    for (unsigned attempt = 1U;; ++attempt) {
        const int16_t state =
            start_multicast_once(runtime, node, counter, active);
        if (state == RADIOLIB_ERR_NONE) {
            ESP_LOGI(kTag,
                     "MULTICAST ACTIVE slot=%u McAddr=%08" PRIX32
                     " Class=C freq=%" PRIu32 "Hz DR%u counter=%s%" PRIu32,
                     static_cast<unsigned>(MULTICAST_GROUP_ID),
                     static_cast<uint32_t>(MULTICAST_MC_ADDR),
                     static_cast<uint32_t>(MULTICAST_FREQUENCY_HZ),
                     static_cast<unsigned>(MULTICAST_DATARATE),
                     counter.counter_valid ? "" : "unset/",
                     counter.last_fcnt_down);
            runtime.update_ui(
                "MULTICAST ACTIVE", "Class C listening", state);
            return true;
        }

        ESP_LOGE(kTag, "start multicast failed: RadioLib %d", state);
        runtime.update_ui("ERROR", "multicast restart", state);
        if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
            return false;
        }
        if (multicast_configuration_error(state)) {
            ESP_LOGE(kTag,
                     "deterministic multicast configuration error; "
                     "radio retries are disabled");
            runtime.hold("CONFIG REQUIRED", "multicast parameters", state);
        }
        delay_ms(seconds_to_ms(retry_backoff_seconds(attempt)));
    }
}

bool persist_unicast_with_pause(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    bool include_session,
    const MulticastCounterState &counter,
    bool *multicast_active)
{
    const bool resume_multicast =
        multicast_active != nullptr && *multicast_active;
    MulticastStoreResult result = save_unicast_once(
        runtime, node, include_session, resume_multicast);
    if (result.ok()) {
        return true;
    }

    /* Do not leave RxC running while durable session state is uncertain. */
    if (resume_multicast) {
        stop_multicast(runtime, node, multicast_active);
    }

    while (!result.ok()) {
        log_store_error(include_session ? "save unicast session"
                                        : "save DevNonce",
                        result);
        if (!store_result_retryable(result)) {
            runtime.hold("ERROR", "unicast NVS invalid",
                         static_cast<int32_t>(result.status));
        }
        runtime.update_ui("ERROR", "NVS commit retry", result.esp_error);
        delay_ms(seconds_to_ms(kStoreRetrySeconds));
        ensure_store_open(runtime);
        result = save_unicast_once(runtime, node, include_session, false);
    }

    if (resume_multicast && node.isActivated()) {
        return restart_multicast_until_ready(
            runtime, node, counter, multicast_active);
    }
    return true;
}

CounterDecision accept_multicast_counter(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastStoreConfig &config,
    uint32_t frame_counter,
    MulticastCounterState *counter,
    bool *multicast_active)
{
    const MulticastCounterState before_write = *counter;
    MulticastStoreResult result =
        multicast_store_accept_multicast_fcnt(
            config, frame_counter, counter);
    if (result.status == MulticastStoreStatus::kReplayRejected) {
        ESP_LOGW(kTag,
                 "MULTICAST replay/duplicate rejected: FCntDown=%" PRIu32
                 " committed=%" PRIu32,
                 frame_counter, counter->last_fcnt_down);
        runtime.update_ui("ERROR", "replay rejected",
                          static_cast<int32_t>(result.status));
        return CounterDecision::kReplay;
    }
    if (result.ok()) {
        return CounterDecision::kAccepted;
    }

    /*
     * A failed commit can still have reached flash. Stop RxC, reopen NVS, and
     * use durable readback to distinguish success from a genuine replay.
     */
    stop_multicast(runtime, node, multicast_active);
    while (!result.ok()) {
        log_store_error("commit multicast FCntDown", result);
        if (!store_result_retryable(result)) {
            invalid_multicast_state(runtime, result);
        }
        runtime.update_ui("ERROR", "McFCnt NVS retry", result.esp_error);
        delay_ms(seconds_to_ms(kStoreRetrySeconds));
        ensure_store_open(runtime);
        result = multicast_store_accept_multicast_fcnt(
            config, frame_counter, counter);
        if (result.status == MulticastStoreStatus::kReplayRejected) {
            const bool confirmed_current_write =
                counter->counter_valid &&
                counter->last_fcnt_down == frame_counter &&
                (!before_write.counter_valid ||
                 frame_counter > before_write.last_fcnt_down);
            if (!restart_multicast_until_ready(
                    runtime, node, *counter, multicast_active)) {
                return CounterDecision::kRejoin;
            }
            if (confirmed_current_write) {
                ESP_LOGW(kTag,
                         "MULTICAST FCntDown=%" PRIu32
                         " recovered by durable readback after NVS error",
                         frame_counter);
                return CounterDecision::kAccepted;
            }
            ESP_LOGW(kTag,
                     "MULTICAST replay/duplicate rejected after NVS retry: "
                     "FCntDown=%" PRIu32,
                     frame_counter);
            return CounterDecision::kReplay;
        }
    }

    return restart_multicast_until_ready(
               runtime, node, *counter, multicast_active)
               ? CounterDecision::kAccepted
               : CounterDecision::kRejoin;
}
