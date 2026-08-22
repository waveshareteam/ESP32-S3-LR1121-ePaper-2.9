/*
 * LoRaWAN regional-band and channel-policy adapter.
 *
 * The subclass exposes only the protected RadioLib state needed for explicit
 * channel policy and reliable frame-counter persistence.
 */
#pragma once

#include <stdint.h>

#include <RadioLib.h>

/**
 * LoRaWAN node with a repeatable board/deployment channel policy.
 */
class ConfiguredLoRaWANNode final : public LoRaWANNode {
public:
    ConfiguredLoRaWANNode(PhysicalLayer *phy,
                          const LoRaWANBand_t *band,
                          uint8_t sub_band);

    /**
     * Reapply deployment-specific channel policy.
     *
     * Single-channel compatibility mode enforces its mask and data rate.
     * Standard mode is intentionally a no-op so CFList and LinkADR state
     * provided by the network remains intact.
     */
    int16_t apply_channel_policy(void);

    /** Return RadioLib's internal counter for the next uplink frame. */
    uint32_t next_uplink_frame_counter(void) const;
};

/** Return the selected regional band, narrowed to one channel when enabled. */
const LoRaWANBand_t &lorawan_configured_band(void);
/** Return a valid band frequency in MHz for the initial LR1121 setup. */
float lorawan_initial_frequency_mhz(const LoRaWANBand_t &band);
/** Apply TX power, ADR, and channel settings in the required order. */
int16_t lorawan_apply_network_policy(ConfiguredLoRaWANNode &node);
