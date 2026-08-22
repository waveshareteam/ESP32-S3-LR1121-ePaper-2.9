/**
 * @file lorawan_activation.cpp
 * @brief LoRaWAN 1.1 OTAA activation and Class C negotiation.
 */

#include "lorawan_runtime.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "lorawan_config.h"
#include "lorawan_persistence.h"

namespace lorawan {
namespace {

constexpr char kTag[] = "lorawan_class_c";

bool activation_succeeded(ClassCNode &node, int16_t state)
{
    return state == RADIOLIB_LORAWAN_NEW_SESSION ||
           state == RADIOLIB_LORAWAN_SESSION_RESTORED ||
           (state == RADIOLIB_ERR_NONE && node.isActivated());
}

uint32_t exponential_join_backoff_seconds(uint32_t attempt)
{
    uint32_t seconds = LORAWAN_JOIN_BACKOFF_BASE_SECONDS;
    for (uint32_t i = 1; i < attempt; ++i) {
        if (seconds >= LORAWAN_JOIN_BACKOFF_MAX_SECONDS / 2U) {
            return LORAWAN_JOIN_BACKOFF_MAX_SECONDS;
        }
        seconds *= 2U;
    }
    return seconds < LORAWAN_JOIN_BACKOFF_MAX_SECONDS
               ? seconds
               : LORAWAN_JOIN_BACKOFF_MAX_SECONDS;
}

}  // namespace

bool activate_otaa_batch(AppContext &context)
{
    int16_t last_state = RADIOLIB_ERR_UNKNOWN;

    for (uint32_t attempt = 1; attempt <= LORAWAN_JOIN_MAX_ATTEMPTS;
         ++attempt) {
        const int16_t config_state = apply_network_config(*context.node);
        if (config_state != RADIOLIB_ERR_NONE) {
            ESP_LOGE(kTag, "configure LoRaWAN baseline failed: %d",
                     config_state);
            post_ui(context, "ERROR", "NETWORK CONFIG", config_state);
            return false;
        }

        char detail[34] = {};
        snprintf(detail, sizeof(detail), "OTAA TRY %" PRIu32 "/%u",
                 attempt, (unsigned)LORAWAN_JOIN_MAX_ATTEMPTS);
        post_ui(context, "JOINING", detail, last_state);
        ESP_LOGI(kTag, "LoRaWAN 1.1 OTAA activation attempt %" PRIu32
                       "/%u",
                 attempt, (unsigned)LORAWAN_JOIN_MAX_ATTEMPTS);

        {
            RadioCriticalSection radio_critical;
            last_state = context.node->activateOTAA();
        }

        /*
         * Every transmitted JoinRequest consumes a DevNonce. Persist it on
         * both success and failure before another activation attempt.
         */
        (void)request_persist(context, PersistKind::kNonces);
        wait_for_pending_persist(context);

        if (activation_succeeded(*context.node, last_state)) {
            context.restored_session =
                last_state == RADIOLIB_LORAWAN_SESSION_RESTORED;
            context.restored_session_check_pending =
                context.restored_session;
            context.reserved_fcnt_highwater =
                context.node->next_fcnt_up();
            context.fcnt_up_exhausted = false;
            ESP_LOGI(kTag, "OTAA %s",
                     context.restored_session
                         ? "session restored"
                         : "new session activated");

            const int16_t post_join_config =
                apply_network_config(*context.node);
            if (post_join_config != RADIOLIB_ERR_NONE) {
                ESP_LOGE(kTag,
                         "reapply channel/ADR/power after activation "
                         "failed: %d",
                         post_join_config);
                post_ui(context, "ERROR", "POST-JOIN CONFIG",
                        post_join_config);
                return false;
            }

            (void)request_persist(context, PersistKind::kSession);
            wait_for_pending_persist(context);
            post_ui(context, "JOINING",
                    context.restored_session
                        ? "SESSION RESTORED"
                        : "NEW SESSION",
                    last_state);
            return true;
        }

        ESP_LOGW(kTag, "OTAA activation failed: RadioLib %d", last_state);
        post_ui(context, "ERROR", "OTAA FAILED", last_state);
        if (attempt < LORAWAN_JOIN_MAX_ATTEMPTS) {
            uint32_t wait_ms =
                seconds_to_ms(exponential_join_backoff_seconds(attempt));
            const uint32_t duty_cycle_wait =
                (uint32_t)context.node->timeUntilUplink();
            if (duty_cycle_wait > wait_ms) {
                wait_ms = duty_cycle_wait;
            }
            ESP_LOGI(kTag, "next OTAA attempt in %u ms",
                     (unsigned)wait_ms);
            wait_with_service(context, wait_ms);
        }
    }

    post_ui(context, "ERROR", "JOIN BATCH BACKOFF", last_state);
    return false;
}

ClassCResult ensure_class_c(AppContext &context)
{
    if (!context.node->isActivated() || context.fcnt_up_exhausted) {
        return ClassCResult::kRejoin;
    }

    if (context.node->getVersionMajor() != 1U) {
        ESP_LOGE(kTag,
                 "network session is not LoRaWAN 1.1; Starter baseline "
                 "requires LoRaWAN 1.1 OTAA");
        post_ui(context, "ERROR", "LORAWAN 1.1 REQUIRED",
                RADIOLIB_ERR_INVALID_MODE);
        return ClassCResult::kRejoin;
    }

    if (context.node->getClass() == RADIOLIB_LORAWAN_CLASS_C) {
        if (!restart_class_c_receive(context)) {
            return ClassCResult::kRetry;
        }

        if (context.restored_session_check_pending) {
            for (uint32_t attempt = 1;
                 attempt <= LORAWAN_RESTORED_SESSION_CHECK_ATTEMPTS;
                 ++attempt) {
                if (!context.restored_session_check_pending) {
                    post_ui(context, "CLASS C ACTIVE",
                            "RESTORED SESSION VALID",
                            RADIOLIB_ERR_NONE, true);
                    return ClassCResult::kActive;
                }

                char detail[34] = {};
                snprintf(detail, sizeof(detail),
                         "RESTORE CHECK %" PRIu32 "/%u", attempt,
                         (unsigned)LORAWAN_RESTORED_SESSION_CHECK_ATTEMPTS);
                const UplinkResult uplink =
                    send_application_uplink(context, true, detail);
                wait_for_pending_persist(context);

                if (context.fcnt_up_exhausted ||
                    !context.node->isActivated() ||
                    uplink.state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
                    return ClassCResult::kRejoin;
                }
                if (uplink.downlink_received || uplink.acknowledged) {
                    context.restored_session_check_pending = false;
                    if (!context.rx_c_running &&
                        !restart_class_c_receive(context)) {
                        return ClassCResult::kRetry;
                    }
                    ESP_LOGI(kTag,
                             "restored Class C session validated by "
                             "server downlink");
                    post_ui(context, "CLASS C ACTIVE",
                            "RESTORED SESSION VALID", uplink.state, true);
                    return ClassCResult::kActive;
                }

                post_ui(context, "ERROR", "RESTORE CHECK NO ACK",
                        uplink.state >= RADIOLIB_ERR_NONE
                            ? RADIOLIB_ERR_RX_TIMEOUT
                            : uplink.state);
                if (attempt < LORAWAN_RESTORED_SESSION_CHECK_ATTEMPTS) {
                    uint32_t wait_ms =
                        seconds_to_ms(LORAWAN_CLASS_C_RETRY_SECONDS);
                    const uint32_t duty_cycle_wait =
                        (uint32_t)context.node->timeUntilUplink();
                    if (duty_cycle_wait > wait_ms) {
                        wait_ms = duty_cycle_wait;
                    }
                    wait_with_service(context, wait_ms);
                }
            }

            context.restored_session_check_pending = false;
            ESP_LOGW(kTag,
                     "restored Class C session received no server "
                     "response; retaining the locally active session");
            post_ui(context, "CLASS C ACTIVE", "RESTORED UNVERIFIED",
                    RADIOLIB_ERR_RX_TIMEOUT, true);
            return ClassCResult::kActive;
        }

        post_ui(context, "CLASS C ACTIVE", "CONTINUOUS RX READY",
                RADIOLIB_ERR_NONE, true);
        return ClassCResult::kActive;
    }

    bool saw_downlink = false;
    uint32_t send_failures = 0;
    for (uint32_t attempt = 1;
         attempt <= LORAWAN_CLASS_C_MAX_ATTEMPTS; ++attempt) {
        /*
         * A valid Class A downlink clears queued uplink MAC commands, so
         * DeviceModeInd must be queued again before each retry.
         */
        const int16_t request_state = context.node->request_class_c();
        if (request_state != RADIOLIB_ERR_NONE) {
            ESP_LOGE(kTag, "queue DeviceModeInd failed: RadioLib %d",
                     request_state);
            post_ui(context, "ERROR", "CLASS C REQUEST", request_state);
            return context.node->isActivated()
                       ? ClassCResult::kRetry
                       : ClassCResult::kRejoin;
        }

        char detail[34] = {};
        snprintf(detail, sizeof(detail), "DEVICE MODE TRY %" PRIu32 "/%u",
                 attempt, (unsigned)LORAWAN_CLASS_C_MAX_ATTEMPTS);
        const UplinkResult uplink =
            send_application_uplink(context, true, detail);
        saw_downlink = saw_downlink || uplink.downlink_received;

        /*
         * LoRaWAN 1.1 changes class only after DeviceModeConf is parsed.
         * This transition uplink is always confirmed, independent of the
         * periodic-uplink setting.
         */
        wait_for_pending_persist(context);
        if (context.fcnt_up_exhausted) {
            return ClassCResult::kRejoin;
        }
        if (context.node->getClass() == RADIOLIB_LORAWAN_CLASS_C) {
            context.restored_session_check_pending = false;
            if (!context.rx_c_running &&
                !restart_class_c_receive(context)) {
                return ClassCResult::kRetry;
            }
            ESP_LOGI(kTag,
                     "DeviceModeConf accepted; Class C continuous RX active");
            post_ui(context, "CLASS C ACTIVE", "DEVICE MODE CONFIRMED",
                    uplink.state, true);
            return ClassCResult::kActive;
        }

        if (!context.node->isActivated() ||
            uplink.state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
            return ClassCResult::kRejoin;
        }

        const bool send_failed =
            (uplink.state < RADIOLIB_ERR_NONE &&
             uplink.state != RADIOLIB_ERR_UPLINK_UNAVAILABLE) ||
            (uplink.state >= RADIOLIB_ERR_NONE && !uplink.acknowledged);
        if (send_failed) {
            ++send_failures;
            if (send_failures >= LORAWAN_SEND_FAILURES_BEFORE_BACKOFF) {
                ESP_LOGW(kTag,
                         "Class C negotiation hit the send-failure "
                         "threshold; retaining session and backing off");
                return ClassCResult::kRetry;
            }
        }

        post_ui(context, "ERROR", "WAIT DEVICE MODE CONF", uplink.state);
        if (attempt < LORAWAN_CLASS_C_MAX_ATTEMPTS) {
            uint32_t wait_ms =
                seconds_to_ms(LORAWAN_CLASS_C_RETRY_SECONDS);
            const uint32_t duty_cycle_wait =
                (uint32_t)context.node->timeUntilUplink();
            if (duty_cycle_wait > wait_ms) {
                wait_ms = duty_cycle_wait;
            }
            wait_with_service(context, wait_ms);
        }
    }

    if (context.restored_session && !saw_downlink) {
        ESP_LOGW(kTag,
                 "restored session received no response during Class C "
                 "negotiation; retaining session and backing off");
        return ClassCResult::kRetry;
    }

    ESP_LOGW(kTag,
             "server did not confirm Class C; keeping the active OTAA "
             "session and backing off");
    post_ui(context, "ERROR", "SERVER MODE CONF NEEDED",
            RADIOLIB_ERR_RX_TIMEOUT);
    return ClassCResult::kRetry;
}

}  // namespace lorawan
