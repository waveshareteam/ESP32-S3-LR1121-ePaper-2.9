#pragma once

#include <cstddef>
#include <cstdint>

#include <RadioLib.h>

#include "abp_state.h"
#include "abp_ui.h"

constexpr int16_t ABP_ERR_FCNT_EXHAUSTED = INT16_C(-32000);

class PersistentAbpNode final : public LoRaWANNode {
public:
    PersistentAbpNode(PhysicalLayer *phy,
                      const LoRaWANBand_t *band,
                      uint8_t sub_band,
                      abp_state::Store &store,
                      const abp_state::Identity &identity,
                      abp_state::Snapshot &snapshot,
                      AbpStatusUi &ui);

    using LoRaWANNode::sendReceive;

    int16_t apply_network_policy();
    void persist_current_or_wait();
    void refresh_frame_counters();
    uint32_t next_fcnt_up() const;

    int16_t sendReceive(const uint8_t *data_up,
                        size_t length_up,
                        uint8_t port,
                        uint8_t *data_down,
                        size_t *length_down,
                        bool confirmed = false,
                        LoRaWANEvent_t *event_up = nullptr,
                        LoRaWANEvent_t *event_down = nullptr) override;

private:
    void capture_buffers();
    void capture_reserved(uint32_t reserved_fcnt_up);
    void persist_or_wait(bool reserved);

    abp_state::Store &store_;
    const abp_state::Identity &identity_;
    abp_state::Snapshot &snapshot_;
    AbpStatusUi &ui_;
};
