#pragma once

#include "lorawan_profile.h"
#include "multicast_store.h"
#include "radio_runtime.h"

/* Signals that the active Class C loop must rebuild the unicast session. */
enum class LoRaWANLoopDecision {
    kContinue,
    kRejoin,
};

/* Restores or joins the device's own OTAA/Class A unicast session. */
bool lorawan_activate_unicast(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastCounterState &counter,
    bool *session_restored);

/* Runs Class C multicast reception and periodic unicast uplinks. */
LoRaWANLoopDecision lorawan_run_active_loop(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    LR1121 &radio,
    const MulticastStoreConfig &store_config,
    MulticastCounterState *counter,
    bool *multicast_active);
