/**
 * @file lorawan_runtime.cpp
 * @brief Continuous Class C receive and application uplink services.
 */

#include "lorawan_runtime.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lorawan_config.h"
#include "lorawan_persistence.h"

namespace lorawan {
namespace {

constexpr char kTag[] = "lorawan_class_c";
constexpr uint8_t kPayloadFormatVersion = 1U;
constexpr size_t kUplinkPayloadSize = 9U;
constexpr int64_t kUsPerMs = 1000LL;
constexpr int64_t kUsPerSecond = 1000000LL;
constexpr uint8_t kStatusSessionRestored = (uint8_t)(1U << 0);
constexpr uint8_t kStatusClassCActive = (uint8_t)(1U << 1);
constexpr uint8_t kStatusPersistPending = (uint8_t)(1U << 2);

static_assert(RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE == 242,
              "RadioLib 7.7.1 downlink limit changed");

void refresh_ui_snapshot(AppContext &context)
{
    context.ui.session_restored = context.restored_session;
    context.ui.class_c_active =
        context.node != nullptr && context.node->isActivated() &&
        context.node->getClass() == RADIOLIB_LORAWAN_CLASS_C;
    if (context.node == nullptr) {
        return;
    }

    context.ui.fcnt_up = context.node->getFCntUp();
    LoRaWANChannel_t rx_c = {};
    context.ui.rx2_valid = context.node->get_class_c_channel(&rx_c);
    if (context.ui.rx2_valid) {
        context.ui.rx2_frequency_mhz = (float)rx_c.freq / 10000.0f;
        context.ui.rx2_datarate = rx_c.dr;
    }
}

bool is_duplicate_downlink(const AppContext &context,
                           const LoRaWANEvent_t &event)
{
    return context.last_downlink.valid &&
           context.last_downlink.frame_counter == event.fCnt &&
           context.last_downlink.port == event.fPort &&
           context.last_downlink.multicast == event.multicast &&
           context.last_downlink.multicast_group == event.mcGroupId;
}

void remember_downlink(AppContext &context, const LoRaWANEvent_t &event)
{
    context.last_downlink.valid = true;
    context.last_downlink.frame_counter = event.fCnt;
    context.last_downlink.port = event.fPort;
    context.last_downlink.multicast = event.multicast;
    context.last_downlink.multicast_group = event.mcGroupId;
}

void log_safe_text(const uint8_t *data, size_t length)
{
    const size_t safe_length =
        length < (size_t)LORAWAN_SAFE_TEXT_MAX_BYTES
            ? length
            : (size_t)LORAWAN_SAFE_TEXT_MAX_BYTES;
    char text[LORAWAN_SAFE_TEXT_MAX_BYTES + 1U] = {};
    for (size_t i = 0; i < safe_length; ++i) {
        text[i] = data[i] >= 0x20U && data[i] <= 0x7EU
                      ? (char)data[i]
                      : '.';
    }
    ESP_LOGI(kTag, "downlink text=\"%s\"%s", text,
             safe_length < length ? " (truncated)" : "");
}

void handle_downlink(AppContext &context, const uint8_t *data, size_t length,
                     const LoRaWANEvent_t &event, int16_t window)
{
    if (length > RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE) {
        ESP_LOGE(kTag, "downlink length %u exceeds buffer limit %u",
                 (unsigned)length,
                 (unsigned)RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE);
        post_ui(context, "ERROR", "DOWNLINK TOO LARGE",
                RADIOLIB_ERR_PACKET_TOO_LONG);
        return;
    }

    const float rssi = context.radio->getRSSI();
    const float snr = context.radio->getSNR();
    const bool duplicate = is_duplicate_downlink(context, event);
    if (context.restored_session_check_pending && !event.multicast) {
        context.restored_session_check_pending = false;
        ESP_LOGI(kTag,
                 "restored session validated by authenticated unicast "
                 "downlink");
    }

    ESP_LOGI(kTag,
             "downlink window=%d port=%u len=%u fcnt=%" PRIu32
             " dr=%u freq=%.4f MHz pending=%s cast=%s",
             (int)window, (unsigned)event.fPort, (unsigned)length,
             event.fCnt, (unsigned)event.datarate, (double)event.freq,
             event.frmPending ? "yes" : "no",
             event.multicast ? "multicast" : "unicast");
    // RadioLib 7.7.1 exposes RSSI and SNR here, but not frequency error.
    ESP_LOGI(kTag, "downlink RSSI=%.1f dBm SNR=%.1f dB freq-error=N/A",
             (double)rssi, (double)snr);

    if (length == 0U) {
        ESP_LOGI(kTag, "downlink has no application payload");
    } else {
        ESP_LOG_BUFFER_HEX_LEVEL(kTag, data, length, ESP_LOG_INFO);
        log_safe_text(data, length);
    }

    if (duplicate) {
        ESP_LOGW(kTag,
                 "duplicate application event suppressed: port=%u fcnt=%"
                 PRIu32,
                 (unsigned)event.fPort, event.fCnt);
    } else {
        ++context.ui.downlink_count;
        remember_downlink(context, event);
    }

    context.ui.last_port = event.fPort;
    context.ui.signal_valid = true;
    context.ui.rssi = rssi;
    context.ui.snr = snr;
    post_ui(context, "RX",
            duplicate ? "DUPLICATE SUPPRESSED"
                      : (length == 0U ? "EMPTY/MAC DOWNLINK"
                                      : "CLASS C DOWNLINK"),
            window);
}

bool restart_class_c_receive_locked(AppContext &context)
{
    if (!context.node->isActivated() ||
        context.node->getClass() != RADIOLIB_LORAWAN_CLASS_C) {
        context.rx_c_running = false;
        return false;
    }

    // resume_class_c() uses RadioLib's session RX2/RxC channel.
    const int16_t state = context.node->resume_class_c();
    if (state == RADIOLIB_ERR_NONE) {
        context.rx_c_running = true;
        context.next_rx_restart_us = 0;
        refresh_ui_snapshot(context);
        ESP_LOGI(kTag, "continuous RxC window opened from session RX2");
        return true;
    }

    context.rx_c_running = false;
    context.next_rx_restart_us =
        esp_timer_get_time() +
        (int64_t)LORAWAN_CLASS_C_RETRY_SECONDS * kUsPerSecond;
    ESP_LOGE(kTag, "open continuous RxC failed: RadioLib %d", state);
    post_ui(context, "ERROR", "RXC RESTART FAILED", state);
    return false;
}

void poll_class_c(AppContext &context)
{
    if (!context.rx_c_running || !context.node->isActivated() ||
        context.node->getClass() != RADIOLIB_LORAWAN_CLASS_C ||
        context.pending_persist != PersistKind::kNone) {
        return;
    }

    uint8_t data[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE] = {};
    size_t length = 0;
    LoRaWANEvent_t event = {};
    int16_t state = RADIOLIB_ERR_UNKNOWN;
    bool receive_restarted = true;
    {
        RadioCriticalSection radio_critical;
        state = context.node->getDownlinkClassC(data, &length, &event);
        if (state != RADIOLIB_ERR_NONE) {
            if (state == RADIOLIB_LORAWAN_RX_BC) {
                handle_downlink(context, data, length, event, state);
            }

            /*
             * Any non-zero result consumes the latched RxDone event. Reopen
             * RxC before the display can acquire the shared SPI bus, even
             * when RadioLib rejected or could not parse the frame.
             */
            context.rx_c_running = false;
            receive_restarted = restart_class_c_receive_locked(context);
        }
    }

    if (state == RADIOLIB_LORAWAN_RX_BC) {
        (void)request_persist(context, PersistKind::kSession);
        if (!receive_restarted) {
            ESP_LOGW(kTag,
                     "Class C downlink parsed, but reopening RxC failed");
        }
        return;
    }
    if (state == RADIOLIB_ERR_NONE) {
        return;
    }

    ESP_LOGW(kTag, "Class C downlink processing failed: RadioLib %d", state);
    post_ui(context, "ERROR", "RXC PACKET ERROR", state);
    if (!receive_restarted) {
        ESP_LOGW(kTag,
                 "Class C packet error handled, but reopening RxC failed");
    }
}

void write_u32_be(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

void build_uplink_payload(AppContext &context,
                          uint8_t payload[kUplinkPayloadSize])
{
    const uint32_t sequence = ++context.uplink_sequence;
    const uint32_t uptime_seconds =
        (uint32_t)(esp_timer_get_time() / kUsPerSecond);
    uint8_t status = (uint8_t)(kPayloadFormatVersion << 4);
    if (context.restored_session) {
        status |= kStatusSessionRestored;
    }
    if (context.node->getClass() == RADIOLIB_LORAWAN_CLASS_C) {
        status |= kStatusClassCActive;
    }
    if (context.pending_persist != PersistKind::kNone) {
        status |= kStatusPersistPending;
    }

    write_u32_be(&payload[0], sequence);
    write_u32_be(&payload[4], uptime_seconds);
    payload[8] = status;
}

}  // namespace

RadioCriticalSection::RadioCriticalSection()
{
    lorawan_ui_radio_critical_enter();
}

RadioCriticalSection::~RadioCriticalSection()
{
    lorawan_ui_radio_critical_exit();
}

void copy_text(char *destination, size_t capacity, const char *source)
{
    if (destination == nullptr || capacity == 0U) {
        return;
    }
    snprintf(destination, capacity, "%s", source != nullptr ? source : "");
}

void post_ui(AppContext &context, const char *status, const char *detail,
             int32_t code, bool force_full_refresh)
{
    copy_text(context.ui.status, sizeof(context.ui.status), status);
    copy_text(context.ui.detail, sizeof(context.ui.detail), detail);
    copy_text(context.ui.region, sizeof(context.ui.region),
              LORAWAN_REGION_NAME);
    context.ui.join_eui = LORAWAN_JOIN_EUI;
    context.ui.last_code = code;
    refresh_ui_snapshot(context);
    const esp_err_t err = lorawan_ui_post(context.ui, force_full_refresh);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(kTag, "queue e-paper state failed: %s",
                 esp_err_to_name(err));
    }
}

uint32_t seconds_to_ms(uint32_t seconds)
{
    if (seconds > UINT32_MAX / 1000U) {
        return UINT32_MAX;
    }
    return seconds * 1000U;
}

bool restart_class_c_receive(AppContext &context)
{
    RadioCriticalSection radio_critical;
    return restart_class_c_receive_locked(context);
}

void service_once(AppContext &context)
{
    const int64_t now = esp_timer_get_time();
    if (!context.rx_c_running && context.node->isActivated() &&
        context.node->getClass() == RADIOLIB_LORAWAN_CLASS_C &&
        (context.next_rx_restart_us == 0 ||
         now >= context.next_rx_restart_us)) {
        (void)restart_class_c_receive(context);
    }

    poll_class_c(context);

    if (context.pending_persist != PersistKind::kNone &&
        (context.next_persist_retry_us == 0 ||
         now >= context.next_persist_retry_us)) {
        (void)try_pending_persist(context);
    }
}

void wait_with_service(AppContext &context, uint32_t wait_ms)
{
    const int64_t deadline =
        esp_timer_get_time() + (int64_t)wait_ms * kUsPerMs;
    while (esp_timer_get_time() < deadline) {
        service_once(context);
        vTaskDelay(pdMS_TO_TICKS(LORAWAN_CLASS_C_POLL_INTERVAL_MS));
    }
}

void wait_for_pending_persist(AppContext &context)
{
    // No new frame may be sent until its counter state is durable.
    while (context.pending_persist != PersistKind::kNone) {
        service_once(context);
        vTaskDelay(pdMS_TO_TICKS(LORAWAN_CLASS_C_POLL_INTERVAL_MS));
    }
}

UplinkResult send_application_uplink(AppContext &context, bool confirmed,
                                     const char *purpose)
{
    UplinkResult result = {};

    const int16_t config_state = apply_network_config(*context.node);
    if (config_state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(kTag,
                 "reapply channel/ADR/power before uplink failed: "
                 "RadioLib %d",
                 config_state);
        post_ui(context, "ERROR", "UPLINK NETWORK CONFIG", config_state);
        result.state = config_state;
        return result;
    }

    const UplinkReservationResult reservation =
        reserve_uplink_window(context);
    if (reservation != UplinkReservationResult::kReady) {
        if (reservation == UplinkReservationResult::kNotActivated) {
            result.state = RADIOLIB_ERR_NETWORK_NOT_JOINED;
        } else if (reservation ==
                   UplinkReservationResult::kCounterExhausted) {
            result.state = RADIOLIB_ERR_INVALID_MODE;
        } else {
            result.state = RADIOLIB_ERR_UNKNOWN;
        }
        return result;
    }

    uint8_t payload[kUplinkPayloadSize] = {};
    build_uplink_payload(context, payload);

    uint8_t downlink[RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE] = {};
    size_t downlink_length = 0;
    LoRaWANEvent_t uplink_event = {};
    LoRaWANEvent_t downlink_event = {};

    ESP_LOGI(kTag,
             "uplink seq=%" PRIu32 " uptime=%" PRIu32
             "s status=0x%02X port=%u confirmed=%s purpose=%s",
             context.uplink_sequence,
             (uint32_t)(esp_timer_get_time() / kUsPerSecond),
             (unsigned)payload[8], (unsigned)LORAWAN_UPLINK_PORT,
             confirmed ? "yes" : "no", purpose);

    {
        RadioCriticalSection radio_critical;
        ++context.ui.uplink_count;
        post_ui(context, "TX", purpose, RADIOLIB_ERR_NONE);

        result.state = context.node->sendReceive(
            payload, sizeof(payload), LORAWAN_UPLINK_PORT,
            downlink, &downlink_length, confirmed,
            &uplink_event, &downlink_event);

        if (result.state >= RADIOLIB_ERR_NONE) {
            ESP_LOGI(kTag,
                     "uplink complete: RadioLib %d fcnt=%" PRIu32
                     " dr=%u freq=%.4f MHz tx=%d dBm",
                     result.state, context.node->getFCntUp(),
                     (unsigned)uplink_event.datarate,
                     (double)uplink_event.freq, (int)uplink_event.power);
        } else {
            ESP_LOGE(kTag,
                     "uplink returned RadioLib %d; session will be "
                     "persisted before any further uplink",
                     result.state);
        }

        if (result.state > RADIOLIB_ERR_NONE) {
            result.downlink_received = true;
            result.acknowledged = downlink_event.confirming;
            handle_downlink(context, downlink, downlink_length,
                            downlink_event, result.state);
        } else if (result.state == RADIOLIB_ERR_NONE) {
            post_ui(context, "TX", "UPLINK COMPLETE", result.state);
        } else {
            post_ui(context, "ERROR", "UPLINK FAILED", result.state);
        }

        if (context.node->isActivated() &&
            context.node->getClass() == RADIOLIB_LORAWAN_CLASS_C) {
            /* Restore RxC before the e-paper task can acquire shared SPI. */
            context.rx_c_running = false;
            (void)restart_class_c_receive_locked(context);
        } else {
            context.rx_c_running = false;
        }
    }

    if (confirmed && result.state >= RADIOLIB_ERR_NONE &&
        !result.acknowledged) {
        ESP_LOGW(kTag, "confirmed uplink completed without server ACK");
        post_ui(context, "ERROR", "CONFIRMED NO ACK",
                RADIOLIB_ERR_RX_TIMEOUT);
    }

    (void)verify_fcnt_highwater(context);

    /*
     * A negative sendReceive result does not prove that no RF frame left the
     * radio. Save the advanced counter after every attempted transmission.
     */
    (void)request_persist(context, PersistKind::kSession);
    return result;
}

}  // namespace lorawan
