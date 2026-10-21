#pragma once

#include <Arduino.h>

// Replace every placeholder. Keys are never printed.
constexpr uint64_t LORAWAN_JOIN_EUI = 0x0000000000000000ULL;
constexpr uint64_t LORAWAN_DEV_EUI = 0x0000000000000000ULL;
constexpr uint8_t LORAWAN_NWK_KEY[16] = {0};
constexpr uint8_t LORAWAN_APP_KEY[16] = {0};
constexpr uint32_t MULTICAST_ADDRESS = 0x00000000UL;
constexpr uint8_t MULTICAST_APP_S_KEY[16] = {0};
constexpr uint8_t MULTICAST_NWK_S_KEY[16] = {0};

