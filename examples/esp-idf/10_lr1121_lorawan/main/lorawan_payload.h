/*
 * Application payload codec and safe downlink logger.
 *
 * This module contains no radio or persistent-configuration side effects.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <RadioLib.h>

/**
 * Fixed uplink format:
 *   byte 0      format version
 *   byte 1      status flags
 *   bytes 2-5   application sequence, big endian
 *   bytes 6-9   uptime seconds, big endian
 *   bytes 10-13 free heap bytes, big endian
 *   byte 14     ESP-IDF reset reason
 */
static constexpr size_t LORAWAN_UPLINK_PAYLOAD_SIZE = 15U;

/** Encode one bounded uplink and return its size, or zero on invalid input. */
size_t lorawan_build_uplink_payload(uint8_t *payload,
                                    size_t capacity,
                                    uint32_t sequence,
                                    bool restored_session,
                                    bool had_downlink);

/**
 * Log a Class A downlink and process only the documented no-op demo command.
 */
void lorawan_log_downlink(const uint8_t *data,
                          size_t length,
                          const LoRaWANEvent_t &event,
                          int16_t rx_window,
                          float rssi,
                          float snr);
