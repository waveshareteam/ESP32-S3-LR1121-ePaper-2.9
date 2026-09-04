/**
 * @file lorawan_node.h
 * @brief RadioLib adapter and network configuration helpers.
 */

#pragma once

#include <stdint.h>

#include <RadioLib.h>

namespace lorawan {

/**
 * Exposes the small set of protected RadioLib operations required by the
 * Class C runtime. Keeping them here isolates version-specific behavior.
 */
class ClassCNode final : public LoRaWANNode {
public:
    ClassCNode(PhysicalLayer *phy, const LoRaWANBand_t *band,
               uint8_t sub_band);

    /** Restricts a dynamic-region test setup to one configured channel. */
    void lock_to_single_channel(const LoRaWANChannel_t &channel);

    /** Queues DeviceModeInd unless Class C is already active or pending. */
    int16_t request_class_c();

    /** Reopens continuous RxC using the channel stored in the session. */
    int16_t resume_class_c();

    /** Returns the current session RXC channel for status reporting. */
    bool get_class_c_channel(LoRaWANChannel_t *channel) const;

    /** Returns the frame counter that RadioLib will use for the next uplink. */
    uint32_t next_fcnt_up() const;

    /**
     * Copies a valid session snapshot with an optional raised FCntUp value,
     * then restores RadioLib's live counter and internal buffer.
     */
    bool make_session_snapshot(
        uint32_t minimum_fcnt_up,
        uint8_t snapshot[RADIOLIB_LORAWAN_SESSION_BUF_SIZE]);
};

/** Returns false while any OTAA identifier or root key is a placeholder. */
bool credentials_configured();

/** Calls beginOTAA with the configured LoRaWAN 1.1 credentials. */
int16_t begin_otaa(ClassCNode &node);

/** Applies ADR, power, and optional single-channel compatibility settings. */
int16_t apply_network_config(ClassCNode &node);

/** Selects a legal frequency for the board-level LR1121 initialization. */
float initial_radio_frequency_mhz();

}  // namespace lorawan
