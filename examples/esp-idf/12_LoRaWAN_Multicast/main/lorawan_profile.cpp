#include "lorawan_profile.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "multicast_config.h"

/*
 * Deployment profile for the verified EU868 setup. This is the only module
 * that owns OTAA and multicast key arrays; no key value is ever logged.
 */
namespace {

constexpr char kTag[] = "lorawan_multicast";

/* Credential bytes stay private to this translation unit and are never logged. */
const uint8_t kAppKey[16] = {LORAWAN_APP_KEY_BYTES};
const uint8_t kNwkKey[16] = {LORAWAN_NWK_KEY_BYTES};
const uint8_t kMcAppSKey[16] = {MULTICAST_MC_APP_S_KEY_BYTES};
const uint8_t kMcNwkSKey[16] = {MULTICAST_MC_NWK_S_KEY_BYTES};

/* RadioLib stores regional frequencies in 100 Hz units in this descriptor. */
LoRaWANBand_t make_single_channel_region()
{
    LoRaWANBand_t region = EU868;
    const uint32_t frequency =
        LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ / 100UL;

    region.txFreqs[0].idx = 0;
    region.txFreqs[0].freq = frequency;
    region.txFreqs[0].drMin = LORAWAN_SINGLE_CHANNEL_DR;
    region.txFreqs[0].drMax = LORAWAN_SINGLE_CHANNEL_DR;
    region.txFreqs[0].dr = LORAWAN_SINGLE_CHANNEL_DR;
    region.txFreqs[1] = {};
    region.txFreqs[2] = {};
    return region;
}

const LoRaWANBand_t kLoRaWANRegion = make_single_channel_region();

/* Zero and 0xFF patterns are provisioning placeholders, never usable keys. */
bool key_is_placeholder(const uint8_t *key)
{
    bool all_zero = true;
    bool all_ff = true;
    for (size_t i = 0; i < 16U; ++i) {
        all_zero = all_zero && key[i] == 0x00U;
        all_ff = all_ff && key[i] == 0xFFU;
    }
    return all_zero || all_ff;
}

}  // namespace

SingleChannelLoRaWANNode::SingleChannelLoRaWANNode(
    PhysicalLayer *phy,
    const LoRaWANBand_t *band,
    uint8_t sub_band)
    : LoRaWANNode(phy, band, sub_band)
{
}

int16_t SingleChannelLoRaWANNode::apply_verified_policy()
{
    setADR(LORAWAN_ADR_ENABLED != 0U);

    /* Keep only the channel proven by example 11 and its matching downlink. */
    memset(dynamicChannels, 0, sizeof(dynamicChannels));
    dynamicChannels[RADIOLIB_LORAWAN_UPLINK][0] =
        kLoRaWANRegion.txFreqs[0];
    dynamicChannels[RADIOLIB_LORAWAN_DOWNLINK][0] =
        kLoRaWANRegion.txFreqs[0];

    memset(channelMasks, 0, sizeof(channelMasks));
    memset(channelFlags, 0, sizeof(channelFlags));
    channelMasks[0] = UINT16_C(0x0001);
    channelFlags[0] = UINT16_C(0x0001);
    return setDatarate(LORAWAN_SINGLE_CHANNEL_DR);
}

const LoRaWANBand_t *lorawan_profile_region()
{
    return &kLoRaWANRegion;
}

bool lorawan_profile_configuration_is_valid()
{
    bool valid = true;
    if (LORAWAN_JOIN_EUI == 0U || LORAWAN_DEV_EUI == 0U) {
        ESP_LOGE(kTag, "OTAA EUI placeholders are still configured");
        valid = false;
    }
    if (key_is_placeholder(kAppKey) || key_is_placeholder(kNwkKey)) {
        ESP_LOGE(kTag, "OTAA key placeholders are still configured");
        valid = false;
    }
    if (MULTICAST_MC_ADDR == 0U) {
        ESP_LOGE(kTag, "multicast address placeholder is still configured");
        valid = false;
    }
    if (key_is_placeholder(kMcAppSKey) ||
        key_is_placeholder(kMcNwkSKey)) {
        ESP_LOGE(kTag, "multicast key placeholders are still configured");
        valid = false;
    }
    if (!valid) {
        ESP_LOGE(kTag,
                 "edit main/lorawan_secrets.h; no radio operation will start");
    }
    return valid;
}

int16_t lorawan_profile_begin_otaa(SingleChannelLoRaWANNode &node)
{
    return node.beginOTAA(
        LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI, kNwkKey, kAppKey);
}

void lorawan_profile_configure_multicast_group(
    MulticastGroup_t &group,
    uint32_t last_fcnt_down)
{
    /* Keep the mapping aligned with RadioLib 7.7.1 MulticastGroup_t. */
    group = {};
    group.cls = MULTICAST_DEVICE_CLASS;
    group.mcAddr = MULTICAST_MC_ADDR;
    memcpy(group.mcAppSKey, kMcAppSKey, sizeof(group.mcAppSKey));
    memcpy(group.mcNwkSKey, kMcNwkSKey, sizeof(group.mcNwkSKey));
    group.mcFCnt = last_fcnt_down;
    group.mcFCntMax = MULTICAST_MC_FCNT_MAX;
    group.rxFCnt = 0U;
    group.mcFreq = MULTICAST_FREQUENCY_HZ;
    group.mcDr = MULTICAST_DATARATE;
}
