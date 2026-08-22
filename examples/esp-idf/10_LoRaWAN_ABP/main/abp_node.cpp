#include "abp_node.h"

#include <algorithm>
#include <cstring>
#include <inttypes.h>

#include "abp_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr uint32_t kMillisecondsPerSecond = 1000U;
const char *const kTag = "abp_node";

void delay_seconds(uint32_t seconds)
{
    while (seconds > 0U) {
        const uint32_t chunk = std::min(seconds, UINT32_C(60));
        vTaskDelay(pdMS_TO_TICKS(chunk * kMillisecondsPerSecond));
        seconds -= chunk;
    }
}

}  // namespace

PersistentAbpNode::PersistentAbpNode(
    PhysicalLayer *phy,
    const LoRaWANBand_t *band,
    uint8_t sub_band,
    abp_state::Store &store,
    const abp_state::Identity &identity,
    abp_state::Snapshot &snapshot,
    AbpStatusUi &ui)
    : LoRaWANNode(phy, band, sub_band),
      store_(store),
      identity_(identity),
      snapshot_(snapshot),
      ui_(ui)
{
}

int16_t PersistentAbpNode::apply_network_policy()
{
    int16_t state = setTxPower(LORAWAN_ABP_TX_POWER_DBM);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }

    setADR(LORAWAN_ABP_ADR_ENABLED != 0);

    std::memset(dynamicChannels, 0, sizeof(dynamicChannels));
    dynamicChannels[RADIOLIB_LORAWAN_UPLINK][0] = band->txFreqs[0];
    dynamicChannels[RADIOLIB_LORAWAN_DOWNLINK][0] = band->txFreqs[0];

    std::memset(channelMasks, 0, sizeof(channelMasks));
    std::memset(channelFlags, 0, sizeof(channelFlags));
    channelMasks[0] = UINT16_C(0x0001);
    channelFlags[0] = UINT16_C(0x0001);
    return setDatarate(LORAWAN_ABP_SINGLE_CHANNEL_DR);
}

uint32_t PersistentAbpNode::next_fcnt_up() const
{
    return fCntUp;
}

void PersistentAbpNode::capture_buffers()
{
    const uint8_t *nonces = getBufferNonces();
    const uint8_t *session = getBufferSession();
    std::memcpy(snapshot_.nonces, nonces, sizeof(snapshot_.nonces));
    std::memcpy(snapshot_.session, session, sizeof(snapshot_.session));
}

void PersistentAbpNode::capture_reserved(uint32_t reserved_fcnt_up)
{
    const uint32_t current_fcnt_up = fCntUp;
    fCntUp = reserved_fcnt_up;
    capture_buffers();
    fCntUp = current_fcnt_up;

    // Rebuild RadioLib's internal session buffer with the live counter.
    (void)getBufferSession();
}

void PersistentAbpNode::refresh_frame_counters()
{
    abp_ui_state_t &status = ui_.state();
    status.fcnt_up = fCntUp;
    status.nfcnt_down = getNFCntDown();
    status.afcnt_down = getAFCntDown();
}

void PersistentAbpNode::persist_or_wait(bool reserved)
{
    uint32_t backoff_seconds = LORAWAN_ABP_NVS_RETRY_BASE_SECONDS;
    while (true) {
        const esp_err_t error =
            reserved ? store_.save_reserved(identity_, &snapshot_)
                     : store_.save_actual(identity_, &snapshot_);
        if (error == ESP_OK) {
            return;
        }

        refresh_frame_counters();
        ESP_LOGE(kTag,
                 "NVS commit failed: %s; radio paused, retry in %" PRIu32 " s",
                 esp_err_to_name(error), backoff_seconds);
        ui_.error("NVS COMMIT", error);
        delay_seconds(backoff_seconds);
        if (backoff_seconds < LORAWAN_ABP_NVS_RETRY_MAX_SECONDS) {
            backoff_seconds = std::min(
                backoff_seconds * 2U,
                static_cast<uint32_t>(LORAWAN_ABP_NVS_RETRY_MAX_SECONDS));
        }
    }
}

void PersistentAbpNode::persist_current_or_wait()
{
    capture_buffers();
    persist_or_wait(false);
}

int16_t PersistentAbpNode::sendReceive(const uint8_t *data_up,
                                       size_t length_up,
                                       uint8_t port,
                                       uint8_t *data_down,
                                       size_t *length_down,
                                       bool confirmed,
                                       LoRaWANEvent_t *event_up,
                                       LoRaWANEvent_t *event_down)
{
    if ((length_up > 0U && data_up == nullptr) ||
        data_down == nullptr || length_down == nullptr) {
        return RADIOLIB_ERR_NULL_POINTER;
    }
    if (!isActivated()) {
        return RADIOLIB_ERR_NETWORK_NOT_JOINED;
    }
    if (fCntUp == UINT32_MAX) {
        ESP_LOGE(kTag, "FCntUp exhausted; refusing further radio transmission");
        return ABP_ERR_FCNT_EXHAUSTED;
    }

    // Commit the next counter before transmission so a reset cannot reuse it.
    const uint32_t reserved_fcnt_up = fCntUp + 1U;
    capture_reserved(reserved_fcnt_up);
    persist_or_wait(true);

    const int16_t state = LoRaWANNode::sendReceive(
        data_up, length_up, port, data_down, length_down,
        confirmed, event_up, event_down);

    // A preflight failure may leave fCntUp unchanged. Keeping the reserved
    // boundary is conservative and prevents a future reuse after an
    // ambiguous radio failure.
    if (fCntUp < reserved_fcnt_up) {
        fCntUp = reserved_fcnt_up;
    }
    capture_buffers();
    persist_or_wait(false);
    return state;
}
