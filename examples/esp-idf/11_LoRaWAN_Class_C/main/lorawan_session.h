/**
 * @file lorawan_session.h
 * @brief NVS storage backend for RadioLib LoRaWAN nonces and sessions.
 */

#pragma once

#include <stddef.h>

#include <RadioLib.h>

#include "esp_err.h"

/** Reports which independent RadioLib state blobs were restored from NVS. */
typedef struct {
    bool nonces_restored;
    bool session_restored;
} lorawan_restore_result_t;

/** Initializes NVS and opens the LoRaWAN namespace without erasing data. */
esp_err_t lorawan_storage_init(void);

/** Restores nonces first, then restores a session only when they are valid. */
esp_err_t lorawan_session_restore(LoRaWANNode &node,
                                  lorawan_restore_result_t *result);

/** Persists OTAA nonce state after every activation attempt. */
esp_err_t lorawan_session_save_nonces(LoRaWANNode &node);

/** Persists a complete session while preserving its FCntUp high-water mark. */
esp_err_t lorawan_session_save(
    LoRaWANNode &node, const uint8_t *session_snapshot,
    size_t session_snapshot_size);

/** Commits a future FCntUp value before the corresponding frames are sent. */
esp_err_t lorawan_session_save_uplink_reservation(
    LoRaWANNode &node, const uint8_t *reserved_session,
    size_t reserved_session_size);

/** Removes only the session blob; OTAA nonce state remains intact. */
esp_err_t lorawan_session_clear_saved_session(void);
