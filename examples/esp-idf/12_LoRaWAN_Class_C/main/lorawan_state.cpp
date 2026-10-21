/**
 * @file lorawan_state.cpp
 * @brief Radio initialization, active Class C loop, and recovery orchestration.
 */

#include "lorawan_runtime.h"

#include <inttypes.h>

#include "board_lorawan_radio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lorawan_config.h"
#include "lorawan_session.h"

namespace lorawan {
namespace {

constexpr char kTag[] = "lorawan_class_c";
constexpr int64_t kUsPerMs = 1000LL;

uint32_t next_uplink_delay_ms(ClassCNode &node)
{
    uint32_t wait_ms = seconds_to_ms(LORAWAN_UPLINK_INTERVAL_SECONDS);
    const uint32_t duty_cycle_wait = (uint32_t)node.timeUntilUplink();
    if (duty_cycle_wait > wait_ms) {
        wait_ms = duty_cycle_wait;
    }
    return wait_ms;
}

void wait_before_radio_active(uint32_t wait_ms)
{
    uint32_t remaining = wait_ms;
    while (remaining > 0U) {
        const uint32_t slice = remaining > 250U ? 250U : remaining;
        vTaskDelay(pdMS_TO_TICKS(slice));
        remaining -= slice;
    }
}

void initialize_storage(AppContext &context)
{
    while (true) {
        const esp_err_t err = lorawan_storage_init();
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "LoRaWAN NVS storage ready");
            return;
        }

        // Erasing NVS automatically could destroy valid nonce history.
        ESP_LOGE(kTag,
                 "NVS init failed: %s; automatic erase is disabled",
                 esp_err_to_name(err));
        post_ui(context, "ERROR", "NVS INIT - NO ERASE", (int32_t)err);
        wait_before_radio_active(LORAWAN_PERSIST_RETRY_MS);
    }
}

LR1121 *initialize_radio(AppContext &context)
{
    while (true) {
        post_ui(context, "RADIO INIT", "LR1121 + TCXO", 0);
        LR1121 *radio = nullptr;
        esp_err_t err = ESP_FAIL;
        {
            RadioCriticalSection radio_critical;
            err = board_lorawan_radio_init(
                initial_radio_frequency_mhz(),
                (int8_t)LORAWAN_TX_POWER_DBM, &radio);
        }
        if (err == ESP_OK && radio != nullptr) {
            ESP_LOGI(kTag, "LR1121 board radio initialized");
            return radio;
        }

        const int16_t radio_error = board_lorawan_radio_last_error();
        ESP_LOGE(kTag,
                 "LR1121 init failed: esp=%s RadioLib=%d; retrying",
                 esp_err_to_name(err), radio_error);
        post_ui(context, "ERROR", "LR1121 INIT FAILED",
                radio_error != RADIOLIB_ERR_NONE
                    ? radio_error
                    : (int32_t)err);
        wait_before_radio_active(
            seconds_to_ms(LORAWAN_RADIO_INIT_RETRY_SECONDS));
    }
}

void restore_saved_state(AppContext &context)
{
    while (true) {
        lorawan_restore_result_t restored = {};
        const esp_err_t err =
            lorawan_session_restore(*context.node, &restored);
        if (err == ESP_OK) {
            ESP_LOGI(kTag,
                     "NVS restore complete: nonces=%s session=%s",
                     restored.nonces_restored ? "yes" : "no",
                     restored.session_restored ? "yes" : "no");
            return;
        }

        ESP_LOGE(kTag, "restore LoRaWAN state failed: %s",
                 esp_err_to_name(err));
        post_ui(context, "ERROR", "NVS RESTORE", (int32_t)err);
        wait_before_radio_active(LORAWAN_PERSIST_RETRY_MS);
    }
}

}  // namespace

ClassCResult run_class_c(AppContext &context)
{
    context.consecutive_send_failures = 0;
    int64_t next_uplink_us =
        esp_timer_get_time() +
        (int64_t)next_uplink_delay_ms(*context.node) * kUsPerMs;

    post_ui(context, "CLASS C ACTIVE", "CONTINUOUS RX READY",
            RADIOLIB_ERR_NONE, true);

    while (true) {
        service_once(context);

        if (context.fcnt_up_exhausted ||
            !context.node->isActivated()) {
            if (context.fcnt_up_exhausted) {
                ESP_LOGW(kTag, "FCntUp reservation exhausted; rejoining");
            } else {
                ESP_LOGW(kTag, "LoRaWAN session is no longer active");
            }
            return ClassCResult::kRejoin;
        }
        if (context.node->getClass() != RADIOLIB_LORAWAN_CLASS_C) {
            ESP_LOGW(kTag, "LoRaWAN node left Class C");
            context.rx_c_running = false;
            return ClassCResult::kRetry;
        }

        const int64_t now = esp_timer_get_time();
        if (context.pending_persist == PersistKind::kNone &&
            now >= next_uplink_us) {
            const bool confirmed = LORAWAN_CONFIRMED_UPLINK != 0;
            const UplinkResult uplink = send_application_uplink(
                context, confirmed, "PERIODIC UPLINK");

            if (context.fcnt_up_exhausted ||
                !context.node->isActivated() ||
                uplink.state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
                return ClassCResult::kRejoin;
            }
            if (!context.rx_c_running) {
                return ClassCResult::kRetry;
            }

            const bool send_failed =
                (uplink.state < RADIOLIB_ERR_NONE &&
                 uplink.state != RADIOLIB_ERR_UPLINK_UNAVAILABLE) ||
                (confirmed && uplink.state >= RADIOLIB_ERR_NONE &&
                 !uplink.acknowledged);
            if (send_failed) {
                ++context.consecutive_send_failures;
                if (context.consecutive_send_failures >=
                    LORAWAN_SEND_FAILURES_BEFORE_BACKOFF) {
                    ESP_LOGW(kTag,
                             "periodic uplink failure threshold reached; "
                             "retaining session and backing off");
                    return ClassCResult::kRetry;
                }
            } else if (uplink.state >= RADIOLIB_ERR_NONE) {
                context.consecutive_send_failures = 0;
            }

            next_uplink_us =
                esp_timer_get_time() +
                (int64_t)next_uplink_delay_ms(*context.node) * kUsPerMs;
        }

        vTaskDelay(pdMS_TO_TICKS(LORAWAN_CLASS_C_POLL_INTERVAL_MS));
    }
}

void clear_session_for_rejoin(AppContext &context)
{
    /*
     * clearSession() preserves OTAA nonces. Calling beginOTAA() again here
     * would reset RadioLib's restored in-memory nonce state.
     */
    {
        RadioCriticalSection radio_critical;
        context.node->clearSession();
    }
    context.rx_c_running = false;
    context.next_rx_restart_us = 0;
    context.restored_session = false;
    context.restored_session_check_pending = false;
    context.reserved_fcnt_highwater = 0;
    context.fcnt_up_exhausted = false;
    context.pending_persist = PersistKind::kNone;
    context.next_persist_retry_us = 0;
    context.last_downlink = {};
    post_ui(context, "JOINING", "CLEAR STALE SESSION", RADIOLIB_ERR_NONE);

    while (true) {
        const esp_err_t err = lorawan_session_clear_saved_session();
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "saved session cleared; nonce state retained");
            return;
        }
        ESP_LOGE(kTag, "clear saved session failed: %s",
                 esp_err_to_name(err));
        post_ui(context, "ERROR", "NVS SESSION CLEAR", (int32_t)err);
        wait_with_service(context, LORAWAN_PERSIST_RETRY_MS);
    }
}

void radio_task(void *)
{
    AppContext context = {};
    copy_text(context.ui.region, sizeof(context.ui.region),
              LORAWAN_REGION_NAME);

    initialize_storage(context);
    context.radio = initialize_radio(context);

    // The region must outlive ClassCNode because RadioLib stores its address.
    const LoRaWANBand_t configured_region = make_configured_lorawan_region();
    ClassCNode node(context.radio, &configured_region,
                    (uint8_t)LORAWAN_SUB_BAND);
    context.node = &node;

    // beginOTAA initializes the LoRaWAN 1.1 node once, before NVS restore.
    const int16_t begin_state = begin_otaa(node);
    if (begin_state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(kTag, "begin LoRaWAN 1.1 OTAA failed: RadioLib %d",
                 begin_state);
        post_ui(context, "ERROR", "OTAA NODE INIT", begin_state, true);
        while (true) {
            wait_before_radio_active(1000U);
        }
    }

    restore_saved_state(context);

#if LORAWAN_SINGLE_CHANNEL_MODE
    ESP_LOGI(kTag,
             "configuration: region=%s sub-band=%u single-channel=%" PRIu32
             " Hz DR%u ADR=off tx=%d dBm app-port=%u interval=%us",
             LORAWAN_REGION_NAME, (unsigned)LORAWAN_SUB_BAND,
             (uint32_t)LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ,
             (unsigned)LORAWAN_SINGLE_CHANNEL_DR,
             (int)LORAWAN_TX_POWER_DBM,
             (unsigned)LORAWAN_UPLINK_PORT,
             (unsigned)LORAWAN_UPLINK_INTERVAL_SECONDS);
#else
    ESP_LOGI(kTag,
             "configuration: region=%s sub-band=%u standard channel plan "
             "ADR=%s tx=%d dBm app-port=%u interval=%us",
             LORAWAN_REGION_NAME, (unsigned)LORAWAN_SUB_BAND,
             LORAWAN_ADR_ENABLED ? "on" : "off",
             (int)LORAWAN_TX_POWER_DBM,
             (unsigned)LORAWAN_UPLINK_PORT,
             (unsigned)LORAWAN_UPLINK_INTERVAL_SECONDS);
#endif
    ESP_LOGI(kTag,
             "Class C uses the session RX2/RxC channel; no application "
             "RX2 frequency is hard-coded");
    ESP_LOGI(kTag, "LoRaWAN MAC=%s regional-parameters=%s",
             LORAWAN_MAC_VERSION_NAME,
             LORAWAN_REG_PARAMS_REVISION_NAME);

    while (true) {
        if (!activate_otaa_batch(context)) {
            ESP_LOGW(kTag, "OTAA batch exhausted; cooldown before retry");
            wait_with_service(
                context,
                seconds_to_ms(LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS));
            continue;
        }

        while (context.node->isActivated()) {
            const ClassCResult class_result = ensure_class_c(context);
            if (class_result == ClassCResult::kRejoin) {
                break;
            }
            if (class_result == ClassCResult::kRetry) {
                wait_with_service(
                    context,
                    seconds_to_ms(LORAWAN_CLASS_C_RETRY_SECONDS));
                continue;
            }

            const ClassCResult active_result = run_class_c(context);
            if (active_result == ClassCResult::kRejoin) {
                break;
            }
            wait_with_service(
                context,
                seconds_to_ms(LORAWAN_CLASS_C_RETRY_SECONDS));
        }

        clear_session_for_rejoin(context);
        post_ui(context, "JOINING", "REJOIN BACKOFF", RADIOLIB_ERR_NONE);
        wait_with_service(
            context,
            seconds_to_ms(LORAWAN_CLASS_C_RETRY_SECONDS));
    }
}

}  // namespace lorawan
