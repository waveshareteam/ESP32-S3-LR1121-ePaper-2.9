#include "radio_runtime.h"

#include <algorithm>
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

/*
 * Shared radio ownership and lightweight UI publication. The single LoRaWAN
 * task uses this mutex around every LR1121 or LoRaWANNode operation.
 */
namespace {

constexpr EventBits_t kRadioIdle = BIT0;
constexpr EventBits_t kRadioJoining = BIT1;
constexpr EventBits_t kRadioClassC = BIT2;
constexpr EventBits_t kRadioTx = BIT3;
constexpr EventBits_t kRadioRecovery = BIT4;
constexpr EventBits_t kRadioStateMask =
    kRadioIdle | kRadioJoining | kRadioClassC | kRadioTx | kRadioRecovery;

constexpr char kTag[] = "lorawan_multicast";

/* Converts the public state enum to its event-group representation. */
EventBits_t state_bit(RadioState state)
{
    switch (state) {
    case RadioState::kJoining:
        return kRadioJoining;
    case RadioState::kClassC:
        return kRadioClassC;
    case RadioState::kTx:
        return kRadioTx;
    case RadioState::kRecovery:
        return kRadioRecovery;
    case RadioState::kIdle:
    default:
        return kRadioIdle;
    }
}

}  // namespace

uint32_t seconds_to_ms(uint32_t seconds)
{
    const uint64_t value = static_cast<uint64_t>(seconds) * 1000ULL;
    return value > UINT32_MAX ? UINT32_MAX
                              : static_cast<uint32_t>(value);
}

void delay_ms(uint32_t duration_ms)
{
    /* Split long waits so the conversion remains valid on every tick rate. */
    while (duration_ms > 0U) {
        const uint32_t chunk = std::min(duration_ms, UINT32_C(60000));
        vTaskDelay(pdMS_TO_TICKS(chunk));
        duration_ms -= chunk;
    }
}

int64_t monotonic_ms()
{
    return esp_timer_get_time() / 1000LL;
}

bool RadioRuntime::initialize()
{
    mutex_ = xSemaphoreCreateMutex();
    events_ = xEventGroupCreate();
    if (mutex_ == nullptr || events_ == nullptr) {
        return false;
    }
    xEventGroupSetBits(events_, kRadioIdle);
    return true;
}

bool RadioRuntime::begin(RadioState state)
{
    if (mutex_ == nullptr || events_ == nullptr) {
        return false;
    }
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    xEventGroupClearBits(events_, kRadioStateMask);
    xEventGroupSetBits(events_, state_bit(state));
    return true;
}

void RadioRuntime::end(RadioState next_state)
{
    xEventGroupClearBits(events_, kRadioStateMask);
    xEventGroupSetBits(events_, state_bit(next_state));
    xSemaphoreGive(mutex_);
}

void RadioRuntime::update_ui(const char *status,
                             const char *detail,
                             int32_t code)
{
    snprintf(ui_.status, sizeof(ui_.status), "%s",
             status != nullptr ? status : "UNKNOWN");
    snprintf(ui_.detail, sizeof(ui_.detail), "%s",
             detail != nullptr ? detail : "");
    ui_.last_code = code;

    const esp_err_t error = multicast_ui_post(ui_);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "queue e-paper update failed: %s",
                 esp_err_to_name(error));
    }
}

[[noreturn]] void RadioRuntime::hold(const char *status,
                                     const char *detail,
                                     int32_t code)
{
    update_ui(status, detail, code);
    while (true) {
        delay_ms(UINT32_C(60000));
    }
}
