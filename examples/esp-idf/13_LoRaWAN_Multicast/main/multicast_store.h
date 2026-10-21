#pragma once

#include <stdint.h>

#include <RadioLib.h>
#include "esp_err.h"

/*
 * Persistent-state contract for the unicast device session and the multicast
 * replay-protection record.
 *
 * Persistence is deliberately split in two:
 * - RadioLib nonce/session buffers belong only to the device unicast session.
 * - The multicast blob stores only the static group identity and the last
 *   accepted multicast downlink counter.
 *
 * This module serializes NVS access. The caller must separately serialize all
 * LoRaWANNode access with the radio mutex.
 */

/* Stable error categories used by the state machine and diagnostic logs. */
enum class MulticastStoreStatus : uint8_t {
    kOk = 0,
    kCreated,
    kNotInitialized,
    kNotFound,
    kInvalidLength,
    kInvalidMagic,
    kUnsupportedSchema,
    kInvalidCrc,
    kInvalidValue,
    kGroupMismatch,
    kConfigMismatch,
    kPrerequisiteFailed,
    kReplayRejected,
    kRadioLibRejected,
    kNvsError,
    kNoMemory,
    kLockError,
};

struct MulticastStoreResult {
    MulticastStoreStatus status;
    esp_err_t esp_error;
    int16_t radiolib_status;

    bool ok() const
    {
        return status == MulticastStoreStatus::kOk ||
               status == MulticastStoreStatus::kCreated;
    }
};

/* Reports nonce and session restoration independently for safe fallback. */
struct UnicastRestoreReport {
    MulticastStoreResult nonces;
    MulticastStoreResult session;
};

/* Public, non-secret fields that bind an NVS counter to one group setup. */
struct MulticastStoreConfig {
    uint8_t group_id;
    uint32_t config_generation;
    uint32_t mc_addr;
    uint32_t frequency_hz;
    uint8_t datarate;
};

/* Last committed multicast downlink counter, if one has been accepted. */
struct MulticastCounterState {
    bool counter_valid;
    uint32_t last_fcnt_down;
};

/* Initializes NVS without automatically erasing it on any error. */
MulticastStoreResult multicast_store_init();
void multicast_store_close();

/*
 * Restores the exact RadioLib-sized unicast buffers, nonces first. The caller
 * must own the radio mutex for every LoRaWANNode getter/setter call; the store
 * mutex serializes NVS access only.
 */
UnicastRestoreReport multicast_store_restore_unicast(LoRaWANNode &node);

/* Persist this after every OTAA attempt so DevNonce cannot roll back. */
MulticastStoreResult multicast_store_save_unicast_nonces(LoRaWANNode &node);

/*
 * Writes the nonce first, then the unicast session, and calls nvs_commit().
 * NVS does not provide a transaction across the two keys. On a partial write,
 * the newer nonce is retained and RadioLib rejects the older session because
 * its nonce signature no longer matches.
 */
MulticastStoreResult multicast_store_save_unicast_state(LoRaWANNode &node);

/*
 * Creates an empty counter record only when no record exists. Corrupt or
 * mismatched records are returned to the caller and are never overwritten.
 */
MulticastStoreResult multicast_store_prepare_multicast(
    const MulticastStoreConfig &config,
    MulticastCounterState *state);

MulticastStoreResult multicast_store_load_multicast(
    const MulticastStoreConfig &config,
    MulticastCounterState *state);

/*
 * Rejects event_fcnt <= the committed value. A successful return guarantees
 * the new value and its CRC-protected metadata were committed before the
 * caller processes the multicast payload.
 */
MulticastStoreResult multicast_store_accept_multicast_fcnt(
    const MulticastStoreConfig &config,
    uint32_t event_fcnt,
    MulticastCounterState *state);

/* Clears only rejected OTAA nonce/session buffers; multicast state is retained. */
MulticastStoreResult multicast_store_clear_unicast_state();

/* Explicit maintenance action: erases only the multicast state key. */
MulticastStoreResult multicast_store_clear_multicast();

/* Returns a short, non-secret name suitable for logs and the e-paper UI. */
const char *multicast_store_status_name(MulticastStoreStatus status);

