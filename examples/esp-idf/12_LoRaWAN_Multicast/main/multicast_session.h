#pragma once

#include "lorawan_profile.h"
#include "multicast_store.h"
#include "radio_runtime.h"

/* Result of committing a multicast downlink counter before payload use. */
enum class CounterDecision {
    kAccepted,
    kReplay,
    kRejoin,
};

/* Opens NVS with retry; this function never erases stored session state. */
void ensure_store_open(RadioRuntime &runtime);

/* Stops on corrupt or mismatched multicast state until an operator fixes it. */
[[noreturn]] void invalid_multicast_state(
    RadioRuntime &runtime,
    const MulticastStoreResult &result);

/* Creates the configured Class C group and retries transient radio errors. */
bool restart_multicast_until_ready(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastCounterState &counter,
    bool *active);

/* Removes the local multicast slot without changing its receive counter. */
bool stop_multicast(RadioRuntime &runtime,
                    SingleChannelLoRaWANNode &node,
                    bool *active);

/*
 * Saves the device's unicast nonce/session state. If NVS fails while Class C
 * is active, reception is paused until persistence becomes reliable again.
 */
bool persist_unicast_with_pause(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    bool include_session,
    const MulticastCounterState &counter,
    bool *multicast_active);

/*
 * Commits McFCntDown before the application sees a multicast payload. This
 * preserves replay protection across resets and uncertain NVS write results.
 */
CounterDecision accept_multicast_counter(
    RadioRuntime &runtime,
    SingleChannelLoRaWANNode &node,
    const MulticastStoreConfig &config,
    uint32_t frame_counter,
    MulticastCounterState *counter,
    bool *multicast_active);

