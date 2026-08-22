#ifndef LORAWAN_ABP_CONFIG_H
#define LORAWAN_ABP_CONFIG_H

#include <stdint.h>
#include <RadioLib.h>

// Default: LoRaWAN 1.0.x ABP. Set to 11 only for a LoRaWAN 1.1 server profile.
#define LORAWAN_ABP_MAC_VERSION_1_0 10U
#define LORAWAN_ABP_MAC_VERSION_1_1 11U
#ifndef LORAWAN_ABP_MAC_VERSION
#define LORAWAN_ABP_MAC_VERSION LORAWAN_ABP_MAC_VERSION_1_0
#endif

#if __has_include("abp_credentials.h")
#include "abp_credentials.h"
#endif

#ifndef LORAWAN_ABP_DEV_ADDR
#define LORAWAN_ABP_DEV_ADDR UINT32_C(0x00000000)
#endif
#ifndef LORAWAN_ABP_APP_S_KEY_BYTES
#define LORAWAN_ABP_APP_S_KEY_BYTES \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif
#ifndef LORAWAN_ABP_NWK_S_KEY_BYTES
#define LORAWAN_ABP_NWK_S_KEY_BYTES \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif
#ifndef LORAWAN_ABP_F_NWK_S_INT_KEY_BYTES
#define LORAWAN_ABP_F_NWK_S_INT_KEY_BYTES \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif
#ifndef LORAWAN_ABP_S_NWK_S_INT_KEY_BYTES
#define LORAWAN_ABP_S_NWK_S_INT_KEY_BYTES \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif
#ifndef LORAWAN_ABP_NWK_S_ENC_KEY_BYTES
#define LORAWAN_ABP_NWK_S_ENC_KEY_BYTES \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif

// Preserve the verified Starter radio/network policy.
#define LORAWAN_ABP_REGION_OBJECT EU868
#define LORAWAN_ABP_REGION_NAME "EU868"
#define LORAWAN_ABP_SUB_BAND 0U
#define LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ UINT32_C(868100000)
#define LORAWAN_ABP_SINGLE_CHANNEL_DR 3U
#define LORAWAN_ABP_TX_POWER_DBM 16
#define LORAWAN_ABP_ADR_ENABLED 0

#define LORAWAN_ABP_UPLINK_PORT 1U
#define LORAWAN_ABP_UPLINK_INTERVAL_SECONDS UINT32_C(60)
#define LORAWAN_ABP_CONFIRMED_UPLINK 0

#define LORAWAN_ABP_RADIO_RETRY_BASE_SECONDS UINT32_C(15)
#define LORAWAN_ABP_ERROR_BACKOFF_BASE_SECONDS UINT32_C(15)
#define LORAWAN_ABP_ERROR_BACKOFF_MAX_SECONDS UINT32_C(300)
#define LORAWAN_ABP_NVS_RETRY_BASE_SECONDS UINT32_C(5)
#define LORAWAN_ABP_NVS_RETRY_MAX_SECONDS UINT32_C(60)

// Deliberate state reset guard. A matching non-zero value clears only the ABP
// namespace and then halts before radio initialization.
#define LORAWAN_ABP_CLEAR_STATE_MAGIC UINT32_C(0x434C5241)
#ifndef LORAWAN_ABP_CLEAR_STATE_CONFIRM
#define LORAWAN_ABP_CLEAR_STATE_CONFIRM UINT32_C(0x00000000)
#endif

static_assert(LORAWAN_ABP_MAC_VERSION == LORAWAN_ABP_MAC_VERSION_1_0 ||
              LORAWAN_ABP_MAC_VERSION == LORAWAN_ABP_MAC_VERSION_1_1,
              "Select LoRaWAN MAC version 10 or 11");
static_assert(LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ >= UINT32_C(863000000) &&
              LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ <= UINT32_C(870000000),
              "The preserved single channel must remain inside EU868");
static_assert((LORAWAN_ABP_SINGLE_CHANNEL_FREQUENCY_HZ % 100U) == 0U,
              "RadioLib LoRaWAN frequencies use 100 Hz units");
static_assert(LORAWAN_ABP_SINGLE_CHANNEL_DR <= 5U,
              "EU868 LoRa datarate must be DR0 through DR5");
static_assert(LORAWAN_ABP_UPLINK_PORT >= 1U &&
              LORAWAN_ABP_UPLINK_PORT <= 223U,
              "Application FPort must be 1 through 223");
static_assert(LORAWAN_ABP_UPLINK_INTERVAL_SECONDS > 0U,
              "Uplink interval must be non-zero");
static_assert(LORAWAN_ABP_ADR_ENABLED == 0,
              "ADR must remain disabled in preserved single-channel mode");
static_assert(LORAWAN_ABP_CLEAR_STATE_CONFIRM == 0U ||
              LORAWAN_ABP_CLEAR_STATE_CONFIRM == LORAWAN_ABP_CLEAR_STATE_MAGIC,
              "Use the documented clear-state confirmation value");

#endif
