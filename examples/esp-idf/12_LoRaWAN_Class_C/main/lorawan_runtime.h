/**
 * @file lorawan_runtime.h
 * @brief Shared state and internal services for the Class C state machine.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lorawan_node.h"
#include "lorawan_ui.h"

namespace lorawan {

/** Serializes multi-step radio operations against e-paper refreshes. */
class RadioCriticalSection final {
public:
    RadioCriticalSection();
    ~RadioCriticalSection();

    RadioCriticalSection(const RadioCriticalSection &) = delete;
    RadioCriticalSection &operator=(const RadioCriticalSection &) = delete;
};

/** Selects which LoRaWAN state must be written before another uplink. */
enum class PersistKind : uint8_t {
    kNone = 0,
    kNonces = 1,
    kSession = 2,
};

/** Result returned by Class C negotiation and the active receive loop. */
enum class ClassCResult : uint8_t {
    kActive,
    kRetry,
    kRejoin,
};

/** Result of reserving a crash-safe FCntUp window. */
enum class UplinkReservationResult : uint8_t {
    kReady,
    kNotActivated,
    kPersistFailed,
    kCounterExhausted,
};

/** Compact result for one sendReceive transaction. */
struct UplinkResult {
    int16_t state = RADIOLIB_ERR_UNKNOWN;
    bool downlink_received = false;
    bool acknowledged = false;
};

/** Identifies a previously reported downlink without storing its payload. */
struct DownlinkSignature {
    bool valid = false;
    uint32_t frame_counter = 0;
    uint8_t port = 0;
    bool multicast = false;
    uint8_t multicast_group = 0;
};

/**
 * Mutable application state. Only the radio task may access the radio and
 * node pointers; the display task receives copies of ui through a queue.
 */
struct AppContext {
    LR1121 *radio = nullptr;
    ClassCNode *node = nullptr;
    LorawanUiState ui = {};

    uint32_t uplink_sequence = 0;
    uint32_t consecutive_send_failures = 0;
    uint32_t reserved_fcnt_highwater = 0;

    bool restored_session = false;
    bool restored_session_check_pending = false;
    bool fcnt_up_exhausted = false;
    bool rx_c_running = false;

    int64_t next_rx_restart_us = 0;
    PersistKind pending_persist = PersistKind::kNone;
    int64_t next_persist_retry_us = 0;
    DownlinkSignature last_downlink = {};
};

/** FreeRTOS entry point that exclusively owns LR1121 and RadioLib. */
void radio_task(void *task_argument);

/** Copies nullable text into a fixed-size, always-terminated buffer. */
void copy_text(char *destination, size_t capacity, const char *source);

/** Updates and queues a complete display snapshot without blocking on I/O. */
void post_ui(AppContext &context, const char *status, const char *detail,
             int32_t code, bool force_full_refresh = false);

/** Converts seconds to milliseconds with saturation. */
uint32_t seconds_to_ms(uint32_t seconds);

/** Services RxC and persistence retries once without a long blocking wait. */
void service_once(AppContext &context);

/** Waits while continuously servicing Class C receive and NVS retries. */
void wait_with_service(AppContext &context, uint32_t wait_ms);

/** Blocks new uplinks until a pending NVS operation succeeds. */
void wait_for_pending_persist(AppContext &context);

/** Reopens continuous receive with the current session RXC parameters. */
bool restart_class_c_receive(AppContext &context);

/** Sends one application frame and restores RxC before releasing the radio. */
UplinkResult send_application_uplink(AppContext &context, bool confirmed,
                                     const char *purpose);

/** Runs one bounded batch of LoRaWAN 1.1 OTAA activation attempts. */
bool activate_otaa_batch(AppContext &context);

/** Negotiates Class C or resumes a restored Class C session. */
ClassCResult ensure_class_c(AppContext &context);

/** Runs continuous receive and periodic uplinks until retry or rejoin. */
ClassCResult run_class_c(AppContext &context);

/** Clears only the saved session while preserving OTAA nonce state. */
void clear_session_for_rejoin(AppContext &context);

}  // namespace lorawan
