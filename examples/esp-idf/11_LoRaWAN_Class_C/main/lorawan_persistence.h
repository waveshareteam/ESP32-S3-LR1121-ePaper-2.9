/**
 * @file lorawan_persistence.h
 * @brief Crash-safe LoRaWAN persistence coordination.
 */

#pragma once

#include "lorawan_runtime.h"

namespace lorawan {

/** Requests immediate nonce or session persistence from the radio task. */
bool request_persist(AppContext &context, PersistKind kind);

/** Retries the pending persistence operation when one is scheduled. */
bool try_pending_persist(AppContext &context);

/** Persists a future FCntUp high-water mark before the next transmission. */
UplinkReservationResult reserve_uplink_window(AppContext &context);

/** Detects whether RadioLib consumed more counters than were reserved. */
bool verify_fcnt_highwater(AppContext &context);

}  // namespace lorawan
