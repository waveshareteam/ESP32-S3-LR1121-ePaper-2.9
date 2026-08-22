/*
 * Builds the RadioLib band object and reapplies deployment-specific radio
 * policy without mixing that policy into the application workflow.
 */
#include "lorawan_node.h"

#include <string.h>

#include "lorawan_config.h"

#if LORAWAN_SINGLE_CHANNEL_MODE
/*
 * Build a copy of the regional table containing only the gateway channel.
 * RadioLib stores LoRaWAN frequencies in units of 100 Hz.
 */
static LoRaWANBand_t make_lorawan_band(void)
{
    LoRaWANBand_t band = LORAWAN_REGION_OBJECT;
    const uint32_t frequency =
        LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ / 100UL;

    band.txFreqs[0].idx = 0;
    band.txFreqs[0].freq = frequency;
    band.txFreqs[0].drMin = LORAWAN_SINGLE_CHANNEL_DR;
    band.txFreqs[0].drMax = LORAWAN_SINGLE_CHANNEL_DR;
    band.txFreqs[0].dr = LORAWAN_SINGLE_CHANNEL_DR;
    band.txFreqs[1] = {};
    band.txFreqs[2] = {};
    return band;
}
#endif

ConfiguredLoRaWANNode::ConfiguredLoRaWANNode(PhysicalLayer *phy,
                                             const LoRaWANBand_t *band,
                                             uint8_t sub_band)
    : LoRaWANNode(phy, band, sub_band)
{
}

uint32_t ConfiguredLoRaWANNode::next_uplink_frame_counter(void) const
{
    return fCntUp;
}

int16_t ConfiguredLoRaWANNode::apply_channel_policy(void)
{
#if LORAWAN_SINGLE_CHANNEL_MODE
    /*
     * Clear both link directions and every mask before enabling channel 0.
     * This prevents restored or server-provided channels from leaking into
     * the explicit single-channel compatibility mode.
     */
    const LoRaWANBand_t &band = lorawan_configured_band();

    memset(dynamicChannels, 0, sizeof(dynamicChannels));
    dynamicChannels[RADIOLIB_LORAWAN_UPLINK][0] = band.txFreqs[0];
    dynamicChannels[RADIOLIB_LORAWAN_DOWNLINK][0] = band.txFreqs[0];

    memset(channelMasks, 0, sizeof(channelMasks));
    memset(channelFlags, 0, sizeof(channelFlags));
    channelMasks[0] = UINT16_C(0x0001);
    channelFlags[0] = UINT16_C(0x0001);
    return setDatarate(LORAWAN_SINGLE_CHANNEL_DR);
#else
    return RADIOLIB_ERR_NONE;
#endif
}

const LoRaWANBand_t &lorawan_configured_band(void)
{
#if LORAWAN_SINGLE_CHANNEL_MODE
    // Keep the band alive because LoRaWANNode stores a pointer to this object.
    static const LoRaWANBand_t band = make_lorawan_band();
    return band;
#else
    return LORAWAN_REGION_OBJECT;
#endif
}

/*
 * RadioLib band frequencies use 100 Hz units, so division by 10000 converts
 * them to the MHz value expected by the board radio initializer.
 */
float lorawan_initial_frequency_mhz(const LoRaWANBand_t &band)
{
    uint32_t frequency = 0;
    if (band.bandType == RADIOLIB_LORAWAN_BAND_DYNAMIC) {
        frequency = band.txFreqs[0].freq;
    } else if (band.numTxSpans > 0U) {
        frequency = band.txSpans[0].freqStart;
    }
    return (float)frequency / 10000.0f;
}

/*
 * Power is validated first, ADR is configured next, and the channel policy is
 * applied last so it can enforce a fixed data rate when required.
 */
int16_t lorawan_apply_network_policy(ConfiguredLoRaWANNode &node)
{
    int16_t state = node.setTxPower(LORAWAN_TX_POWER_DBM);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }

    node.setADR(LORAWAN_ADR_ENABLED != 0);
    return node.apply_channel_policy();
}
