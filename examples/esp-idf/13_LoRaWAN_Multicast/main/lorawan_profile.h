#pragma once

#include <stdint.h>

#include <RadioLib.h>

/*
 * RadioLib node specialization for the verified single-channel EU868 setup.
 * Multicast remains independent of this device's OTAA/Class A session.
 */
class SingleChannelLoRaWANNode final : public LoRaWANNode {
public:
    SingleChannelLoRaWANNode(PhysicalLayer *phy,
                             const LoRaWANBand_t *band,
                             uint8_t sub_band);

    /* Reapplies the channel mask after OTAA restore or a fresh join. */
    int16_t apply_verified_policy();
};

/* Returns the immutable EU868 region descriptor used by this example. */
const LoRaWANBand_t *lorawan_profile_region();

/* Rejects placeholder credentials before any radio operation can start. */
bool lorawan_profile_configuration_is_valid();

/* Starts RadioLib OTAA using private key arrays kept inside the source file. */
int16_t lorawan_profile_begin_otaa(SingleChannelLoRaWANNode &node);

/*
 * Fills a RadioLib Class C group without exposing multicast keys through the
 * public module interface. last_fcnt_down is the durable counter restored
 * from NVS and must never be lower than the server-side group counter.
 */
void lorawan_profile_configure_multicast_group(
    MulticastGroup_t &group,
    uint32_t last_fcnt_down);
