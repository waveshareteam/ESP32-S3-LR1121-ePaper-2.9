/**
 * @file lorawan_persistence.cpp
 * @brief Coordinates crash-safe nonce, session, and FCntUp persistence.
 */

#include "lorawan_persistence.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lorawan_config.h"
#include "lorawan_session.h"

namespace lorawan {
namespace {

constexpr char kTag[] = "lorawan_persist";
constexpr int64_t kUsPerMs = 1000LL;

const char *persist_kind_name(PersistKind kind)
{
    return kind == PersistKind::kSession ? "session" : "nonce";
}

void schedule_persist(AppContext &context, PersistKind kind)
{
    // A session snapshot also contains the current nonce state, so it wins.
    if (static_cast<uint8_t>(kind) >
        static_cast<uint8_t>(context.pending_persist)) {
        context.pending_persist = kind;
    }
    context.next_persist_retry_us = 0;
}

}  // namespace

bool verify_fcnt_highwater(AppContext &context)
{
    const uint32_t next_fcnt_up = context.node->next_fcnt_up();
    if (next_fcnt_up <= context.reserved_fcnt_highwater) {
        return true;
    }

    // Never send after RadioLib has crossed the value committed to NVS.
    context.fcnt_up_exhausted = true;
    ESP_LOGE(kTag,
             "RadioLib consumed FCntUp beyond the persisted "
             "high-water mark (%" PRIu32 " > %" PRIu32 "); "
             "forcing a new OTAA session",
             next_fcnt_up, context.reserved_fcnt_highwater);
    post_ui(context, "ERROR", "FCNT WINDOW OVERRUN",
            ESP_ERR_INVALID_STATE);
    return false;
}

bool try_pending_persist(AppContext &context)
{
    if (context.pending_persist == PersistKind::kNone) {
        return true;
    }

    const PersistKind requested = context.pending_persist;
    if (requested == PersistKind::kSession &&
        !context.node->isActivated()) {
        ESP_LOGW(kTag,
                 "session became inactive before persistence; "
                 "rejoin path will clear the saved session");
        context.pending_persist = PersistKind::kNone;
        context.next_persist_retry_us = 0;
        return true;
    }

    esp_err_t err = ESP_OK;
    if (requested == PersistKind::kSession) {
        uint8_t snapshot[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};
        if (!context.node->make_session_snapshot(
                context.reserved_fcnt_highwater, snapshot)) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            err = lorawan_session_save(*context.node, snapshot,
                                       sizeof(snapshot));
        }
    } else {
        err = lorawan_session_save_nonces(*context.node);
    }

    if (err == ESP_OK) {
        ESP_LOGI(kTag, "LoRaWAN %s state persisted",
                 persist_kind_name(requested));
        context.pending_persist = PersistKind::kNone;
        context.next_persist_retry_us = 0;
        return true;
    }

    // Keep the request pending; service_once() will retry at this deadline.
    context.next_persist_retry_us =
        esp_timer_get_time() +
        static_cast<int64_t>(LORAWAN_PERSIST_RETRY_MS) * kUsPerMs;
    ESP_LOGE(kTag,
             "persist LoRaWAN %s state failed: %s; new uplinks paused",
             persist_kind_name(requested), esp_err_to_name(err));
    post_ui(context, "ERROR",
            requested == PersistKind::kSession
                ? "NVS SESSION SAVE"
                : "NVS NONCE SAVE",
            static_cast<int32_t>(err));
    return false;
}

bool request_persist(AppContext &context, PersistKind kind)
{
    schedule_persist(context, kind);
    return try_pending_persist(context);
}

UplinkReservationResult reserve_uplink_window(AppContext &context)
{
    if (!context.node->isActivated()) {
        return UplinkReservationResult::kNotActivated;
    }

    const uint32_t current_fcnt_up = context.node->next_fcnt_up();
    if (current_fcnt_up >
        UINT32_MAX - LORAWAN_FCNT_UP_RESERVATION_WINDOW) {
        context.fcnt_up_exhausted = true;
        ESP_LOGE(kTag,
                 "FCntUp cannot reserve another %u-frame window; "
                 "a new OTAA session is required",
                 static_cast<unsigned>(
                     LORAWAN_FCNT_UP_RESERVATION_WINDOW));
        post_ui(context, "ERROR", "FCNT EXHAUSTED - REJOIN",
                ESP_ERR_INVALID_STATE);
        return UplinkReservationResult::kCounterExhausted;
    }

    const uint32_t new_highwater =
        current_fcnt_up + LORAWAN_FCNT_UP_RESERVATION_WINDOW;
    if (context.reserved_fcnt_highwater >= new_highwater) {
        return UplinkReservationResult::kReady;
    }

    // Commit future counters before allowing RadioLib to transmit one frame.
    uint8_t reserved_session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};
    if (!context.node->make_session_snapshot(new_highwater,
                                             reserved_session)) {
        ESP_LOGE(kTag, "cannot create FCntUp reservation snapshot");
        post_ui(context, "ERROR", "FCNT RESERVE STATE",
                ESP_ERR_INVALID_STATE);
        return UplinkReservationResult::kNotActivated;
    }

    const esp_err_t reservation_err =
        lorawan_session_save_uplink_reservation(
            *context.node, reserved_session, sizeof(reserved_session));
    if (reservation_err != ESP_OK) {
        ESP_LOGE(kTag, "persist FCntUp reservation failed: %s",
                 esp_err_to_name(reservation_err));
        post_ui(context, "ERROR", "FCNT RESERVE NVS",
                static_cast<int32_t>(reservation_err));
        (void)request_persist(context, PersistKind::kSession);
        return UplinkReservationResult::kPersistFailed;
    }

    context.reserved_fcnt_highwater = new_highwater;
    ESP_LOGI(kTag,
             "reserved FCntUp window [%" PRIu32 ", %" PRIu32 ")",
             current_fcnt_up, new_highwater);
    return UplinkReservationResult::kReady;
}

}  // namespace lorawan
