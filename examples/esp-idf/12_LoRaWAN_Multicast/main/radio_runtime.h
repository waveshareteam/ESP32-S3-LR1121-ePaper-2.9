#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "multicast_ui.h"

/*
 * Serializes all LoRaWANNode/LR1121 access and publishes the current radio
 * phase through an event group. The display task never owns this runtime.
 */
enum class RadioState : uint8_t {
    kIdle,
    kJoining,
    kClassC,
    kTx,
    kRecovery,
};

class RadioRuntime {
public:
    /* Allocates the radio mutex and state event group. */
    bool initialize();

    /* Locks the radio and marks the operation that is about to run. */
    bool begin(RadioState state);

    /* Publishes the next phase before releasing radio ownership. */
    void end(RadioState next_state);

    /* Mutable UI statistics are owned by the single radio task. */
    MulticastUiState &ui() { return ui_; }

    /* Posts a rate-limited display snapshot without blocking the radio task. */
    void update_ui(const char *status, const char *detail, int32_t code);

    /* Displays a terminal error state while keeping the device responsive. */
    [[noreturn]] void hold(const char *status,
                           const char *detail,
                           int32_t code);

private:
    SemaphoreHandle_t mutex_ = nullptr;
    EventGroupHandle_t events_ = nullptr;
    MulticastUiState ui_ = {};
};

/* Saturating time helpers keep long recovery waits safe on FreeRTOS ticks. */
uint32_t seconds_to_ms(uint32_t seconds);
void delay_ms(uint32_t duration_ms);
int64_t monotonic_ms();

