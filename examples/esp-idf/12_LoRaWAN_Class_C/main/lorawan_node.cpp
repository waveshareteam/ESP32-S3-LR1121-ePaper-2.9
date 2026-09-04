/**
 * @file lorawan_node.cpp
 * @brief RadioLib 7.7.1 adapter and LoRaWAN network configuration.
 */

#include "lorawan_node.h"

#include <stddef.h>
#include <string.h>

#include "lorawan_config.h"

namespace lorawan {
namespace {

constexpr size_t kAesKeySize = 16U;
constexpr uint64_t kJoinEui = LORAWAN_JOIN_EUI;
constexpr uint64_t kDevEui = LORAWAN_DEV_EUI;
constexpr uint8_t kAppKey[kAesKeySize] = LORAWAN_APP_KEY_BYTES;
constexpr uint8_t kNwkKey[kAesKeySize] = LORAWAN_NWK_KEY_BYTES;

static_assert(sizeof(kAppKey) == kAesKeySize, "AppKey must be 16 bytes");
static_assert(sizeof(kNwkKey) == kAesKeySize, "NwkKey must be 16 bytes");

bool all_bytes_equal(const uint8_t *bytes, size_t length, uint8_t value)
{
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

}  // namespace

ClassCNode::ClassCNode(PhysicalLayer *phy, const LoRaWANBand_t *band,
                       uint8_t sub_band)
    : LoRaWANNode(phy, band, sub_band)
{
}

void ClassCNode::lock_to_single_channel(const LoRaWANChannel_t &channel)
{
    // Keep uplink and downlink selection on the explicitly configured channel.
    memset(dynamicChannels, 0, sizeof(dynamicChannels));
    dynamicChannels[RADIOLIB_LORAWAN_UPLINK][0] = channel;
    dynamicChannels[RADIOLIB_LORAWAN_DOWNLINK][0] = channel;

    memset(channelMasks, 0, sizeof(channelMasks));
    memset(channelFlags, 0, sizeof(channelFlags));
    channelMasks[0] = 0x0001U;
    channelFlags[0] = 0x0001U;
}

int16_t ClassCNode::request_class_c()
{
    if (!isActivated()) {
        return RADIOLIB_ERR_NETWORK_NOT_JOINED;
    }
    if (getClass() == RADIOLIB_LORAWAN_CLASS_C) {
        return RADIOLIB_ERR_NONE;
    }

    /*
     * LoRaWAN 1.1 changes class only after DeviceModeConf. Avoid queueing a
     * duplicate DeviceModeInd while the previous request is still pending.
     */
    uint8_t pending_class = RADIOLIB_LORAWAN_CLASS_A;
    const int16_t pending_state = getMacPayload(
        RADIOLIB_LORAWAN_MAC_DEVICE_MODE, fOptsUp, fOptsUpLen,
        &pending_class, RADIOLIB_LORAWAN_UPLINK);
    if (pending_state == RADIOLIB_ERR_NONE) {
        return pending_class == RADIOLIB_LORAWAN_CLASS_C
                   ? RADIOLIB_ERR_NONE
                   : RADIOLIB_ERR_INVALID_MODE;
    }
    if (pending_state != RADIOLIB_ERR_COMMAND_QUEUE_ITEM_NOT_FOUND) {
        return pending_state;
    }
    return setClass(RADIOLIB_LORAWAN_CLASS_C);
}

int16_t ClassCNode::resume_class_c()
{
    if (!isActivated()) {
        return RADIOLIB_ERR_NETWORK_NOT_JOINED;
    }
    if (getClass() != RADIOLIB_LORAWAN_CLASS_C) {
        return RADIOLIB_ERR_INVALID_MODE;
    }

    // receiveClassC() uses the RXC channel restored or derived by RadioLib.
    return receiveClassC();
}

bool ClassCNode::get_class_c_channel(LoRaWANChannel_t *channel) const
{
    if (channel == nullptr) {
        return false;
    }

    *channel = channels[RADIOLIB_LORAWAN_RX_BC];
    return channel->freq != 0U &&
           channel->dr != RADIOLIB_LORAWAN_DATA_RATE_UNUSED;
}

uint32_t ClassCNode::next_fcnt_up() const
{
    return fCntUp;
}

bool ClassCNode::make_session_snapshot(
    uint32_t minimum_fcnt_up,
    uint8_t snapshot[RADIOLIB_LORAWAN_SESSION_BUF_SIZE])
{
    if (snapshot == nullptr || !isActivated()) {
        return false;
    }

    /*
     * Serialize a crash-safe FCntUp high-water mark without advancing the
     * live session. Calling getBufferSession() again refreshes RadioLib's
     * internal serialized buffer after the live counter is restored.
     */
    const uint32_t current_fcnt_up = fCntUp;
    if (minimum_fcnt_up > fCntUp) {
        fCntUp = minimum_fcnt_up;
    }
    memcpy(snapshot, getBufferSession(),
           RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    fCntUp = current_fcnt_up;
    (void)getBufferSession();
    return true;
}

bool credentials_configured()
{
    const bool eui_placeholder =
        kDevEui == UINT64_C(0) || kDevEui == UINT64_MAX ||
        kJoinEui == UINT64_MAX;
    const bool app_placeholder =
        all_bytes_equal(kAppKey, sizeof(kAppKey), 0x00U) ||
        all_bytes_equal(kAppKey, sizeof(kAppKey), 0xFFU);
    const bool nwk_placeholder =
        all_bytes_equal(kNwkKey, sizeof(kNwkKey), 0x00U) ||
        all_bytes_equal(kNwkKey, sizeof(kNwkKey), 0xFFU);
    return !eui_placeholder && !app_placeholder && !nwk_placeholder;
}

int16_t begin_otaa(ClassCNode &node)
{
    // The parameter order matches RadioLib 7.7.1 and LoRaWAN 1.1 OTAA.
    return node.beginOTAA(kJoinEui, kDevEui, kNwkKey, kAppKey);
}

int16_t apply_network_config(ClassCNode &node)
{
    node.setADR(LORAWAN_ADR_ENABLED != 0);

    int16_t state = node.setTxPower((int8_t)LORAWAN_TX_POWER_DBM);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }

#if LORAWAN_SINGLE_CHANNEL_MODE
    // This compatibility path is intentionally disabled for normal networks.
    LoRaWANChannel_t channel = LORAWAN_REGION_OBJECT.txFreqs[0];
    channel.idx = 0U;
    channel.freq = LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ / 100U;
    channel.drMin = LORAWAN_SINGLE_CHANNEL_DR;
    channel.drMax = LORAWAN_SINGLE_CHANNEL_DR;
    channel.dr = LORAWAN_SINGLE_CHANNEL_DR;
    node.lock_to_single_channel(channel);
    state = node.setDatarate(LORAWAN_SINGLE_CHANNEL_DR);
#endif
    return state;
}

float initial_radio_frequency_mhz()
{
#if LORAWAN_SINGLE_CHANNEL_MODE
    return (float)LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ / 1000000.0f;
#else
    /*
     * RadioLib band frequencies use 100 Hz units. Prefer the region's RX2
     * value for initialization, then let the activated session own RXC.
     */
    const uint32_t frequency = LORAWAN_REGION_OBJECT.rx2.freq != 0U
                                   ? LORAWAN_REGION_OBJECT.rx2.freq
                                   : LORAWAN_REGION_OBJECT.freqMin;
    return (float)frequency / 10000.0f;
#endif
}

}  // namespace lorawan
