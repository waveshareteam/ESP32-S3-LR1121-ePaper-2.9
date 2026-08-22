/*
 * RadioLib-native LoRaWAN nonce and session persistence.
 *
 * The NVS layout intentionally matches the known-working Starter example.
 */
#pragma once

#include <RadioLib.h>

#include "esp_err.h"

/** Reports restored RadioLib buffers and one-time policy migration. */
typedef struct {
    bool nonces_restored;
    bool session_restored;
    bool session_discarded_for_policy;
    bool incompatible_otaa_cache_cleared;
} lorawan_restore_result_t;

/** Initialize NVS and open the dedicated LoRaWAN namespace. */
esp_err_t lorawan_storage_init(void);

/**
 * Restore nonce state, validate channel policy, then restore the session.
 */
esp_err_t lorawan_session_restore(LoRaWANNode &node,
                                  lorawan_restore_result_t *result);

/** Commit the nonce buffer immediately after an OTAA attempt. */
esp_err_t lorawan_session_save_nonces(LoRaWANNode &node);
/** Commit both nonce and complete session buffers. */
esp_err_t lorawan_session_save(LoRaWANNode &node);
/** Erase only the session blob while preserving the nonce history. */
esp_err_t lorawan_session_clear_saved_session(void);
