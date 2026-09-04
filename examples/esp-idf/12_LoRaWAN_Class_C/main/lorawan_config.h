/**
 * @file lorawan_config.h
 * @brief Central user configuration and compile-time safety checks.
 */

#pragma once

#include <stdint.h>

#include <RadioLib.h>

/*
 * Copy lorawan_secrets.h.example to lorawan_secrets.h and replace every
 * placeholder. The local file is ignored. Credentials are never logged.
 */
#if defined(__has_include)
#if __has_include("lorawan_secrets.h")
#include "lorawan_secrets.h"
#endif
#endif

#ifndef LORAWAN_JOIN_EUI
#define LORAWAN_JOIN_EUI UINT64_MAX
#endif

#ifndef LORAWAN_DEV_EUI
#define LORAWAN_DEV_EUI UINT64_MAX
#endif

#ifndef LORAWAN_APP_KEY_BYTES
#define LORAWAN_APP_KEY_BYTES                                                  \
    {                                                                          \
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                       \
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF                        \
    }
#endif

#ifndef LORAWAN_NWK_KEY_BYTES
#define LORAWAN_NWK_KEY_BYTES                                                  \
    {                                                                          \
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                       \
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF                        \
    }
#endif

/* The known-working Starter uses LoRaWAN 1.1 OTAA. */
#define LORAWAN_USE_VERSION_1_1 1

#define LORAWAN_REGION_EU868   1
#define LORAWAN_REGION_US915   2
#define LORAWAN_REGION_EU433   3
#define LORAWAN_REGION_AU915   4
#define LORAWAN_REGION_CN470   5
#define LORAWAN_REGION_AS923   6
#define LORAWAN_REGION_AS923_2 7
#define LORAWAN_REGION_AS923_3 8
#define LORAWAN_REGION_AS923_4 9
#define LORAWAN_REGION_KR920   10
#define LORAWAN_REGION_IN865   11

/* User configuration starts here. */
#define LORAWAN_REGION_SELECTION LORAWAN_REGION_EU868
#define LORAWAN_SUB_BAND 0U

/*
 * Compatibility with the same single-channel EU868 gateway used by Starter.
 * Set this to 0 for a standards-compliant multi-channel EU868 network.
 */
#define LORAWAN_SINGLE_CHANNEL_MODE          0
#define LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ  868100000UL
#define LORAWAN_SINGLE_CHANNEL_DR            3U

#define LORAWAN_UPLINK_PORT                  1U
#define LORAWAN_UPLINK_INTERVAL_SECONDS      60U
#define LORAWAN_CONFIRMED_UPLINK             0
#define LORAWAN_ADR_ENABLED                  0
#define LORAWAN_TX_POWER_DBM                 16

#define LORAWAN_DISPLAY_MIN_INTERVAL_MS      30000U
#define LORAWAN_CLASS_C_POLL_INTERVAL_MS     10U

#define LORAWAN_JOIN_MAX_ATTEMPTS            5U
#define LORAWAN_JOIN_BACKOFF_BASE_SECONDS    15U
#define LORAWAN_JOIN_BACKOFF_MAX_SECONDS     300U
#define LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS  900U
#define LORAWAN_CLASS_C_MAX_ATTEMPTS         5U
#define LORAWAN_RESTORED_SESSION_CHECK_ATTEMPTS 3U
#define LORAWAN_CLASS_C_RETRY_SECONDS        30U
#define LORAWAN_SEND_FAILURES_BEFORE_BACKOFF 3U
#define LORAWAN_RADIO_INIT_RETRY_SECONDS     60U
#define LORAWAN_PERSIST_RETRY_MS             5000U
#define LORAWAN_FCNT_UP_RESERVATION_WINDOW   32U

#define LORAWAN_DISPLAY_TASK_STACK           8192U
#define LORAWAN_DISPLAY_TASK_PRIORITY        2U
#define LORAWAN_RADIO_TASK_STACK             16384U
#define LORAWAN_RADIO_TASK_PRIORITY          5U
#define LORAWAN_DISPLAY_FIRST_TIMEOUT_MS      30000U
#define LORAWAN_SAFE_TEXT_MAX_BYTES          96U
/* User configuration ends here. */

#define LORAWAN_MAC_VERSION_NAME "1.1.0"
#define LORAWAN_REG_PARAMS_REVISION_NAME "RP002-1.0.4"

#if LORAWAN_REGION_SELECTION == LORAWAN_REGION_EU868
#define LORAWAN_REGION_OBJECT EU868
#define LORAWAN_REGION_NAME "EU868"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_US915
#define LORAWAN_REGION_OBJECT US915
#define LORAWAN_REGION_NAME "US915"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_EU433
#define LORAWAN_REGION_OBJECT EU433
#define LORAWAN_REGION_NAME "EU433"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_AU915
#define LORAWAN_REGION_OBJECT AU915
#define LORAWAN_REGION_NAME "AU915"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_CN470
#define LORAWAN_REGION_OBJECT CN470
#define LORAWAN_REGION_NAME "CN470"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_AS923
#define LORAWAN_REGION_OBJECT AS923
#define LORAWAN_REGION_NAME "AS923"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_AS923_2
#define LORAWAN_REGION_OBJECT AS923_2
#define LORAWAN_REGION_NAME "AS923-2"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_AS923_3
#define LORAWAN_REGION_OBJECT AS923_3
#define LORAWAN_REGION_NAME "AS923-3"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_AS923_4
#define LORAWAN_REGION_OBJECT AS923_4
#define LORAWAN_REGION_NAME "AS923-4"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_KR920
#define LORAWAN_REGION_OBJECT KR920
#define LORAWAN_REGION_NAME "KR920"
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_IN865
#define LORAWAN_REGION_OBJECT IN865
#define LORAWAN_REGION_NAME "IN865"
#else
#error "Unsupported LORAWAN_REGION_SELECTION"
#endif

/**
 * Returns a private region copy so optional compatibility changes cannot
 * mutate RadioLib's shared regional definition.
 */
static inline LoRaWANBand_t make_configured_lorawan_region()
{
    LoRaWANBand_t region = LORAWAN_REGION_OBJECT;
#if LORAWAN_SINGLE_CHANNEL_MODE
    const uint32_t frequency =
        LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ / 100UL;
    region.txFreqs[0].idx = 0U;
    region.txFreqs[0].freq = frequency;
    region.txFreqs[0].drMin = LORAWAN_SINGLE_CHANNEL_DR;
    region.txFreqs[0].drMax = LORAWAN_SINGLE_CHANNEL_DR;
    region.txFreqs[0].dr = LORAWAN_SINGLE_CHANNEL_DR;
    region.txFreqs[1] = {};
    region.txFreqs[2] = {};
#endif
    return region;
}

/* Reject configuration drift before it can change the proven baseline. */
#if LORAWAN_USE_VERSION_1_1 != 1
#error "This example preserves the Starter LoRaWAN 1.1 OTAA activation"
#endif

#if (RADIOLIB_VERSION_MAJOR != 7) || (RADIOLIB_VERSION_MINOR != 7) ||           \
    (RADIOLIB_VERSION_PATCH != 1)
#error "Class C API audit is pinned to RadioLib 7.7.1"
#endif

#if (LORAWAN_REGION_SELECTION == LORAWAN_REGION_US915) ||                       \
    (LORAWAN_REGION_SELECTION == LORAWAN_REGION_AU915)
#if (LORAWAN_SUB_BAND < 1U) || (LORAWAN_SUB_BAND > 8U)
#error "US915 and AU915 require LORAWAN_SUB_BAND in 1..8"
#endif
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_CN470
#if (LORAWAN_SUB_BAND < 1U) || (LORAWAN_SUB_BAND > 12U)
#error "CN470 requires LORAWAN_SUB_BAND in 1..12"
#endif
#elif LORAWAN_SUB_BAND != 0U
#error "This dynamic region requires LORAWAN_SUB_BAND 0"
#endif

#if (LORAWAN_SINGLE_CHANNEL_MODE != 0) && (LORAWAN_SINGLE_CHANNEL_MODE != 1)
#error "LORAWAN_SINGLE_CHANNEL_MODE must be 0 or 1"
#endif

#if LORAWAN_SINGLE_CHANNEL_MODE
#if LORAWAN_REGION_SELECTION != LORAWAN_REGION_EU868
#error "Single-channel compatibility is defined for EU868 only"
#endif
#if (LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ < 863000000UL) ||                     \
    (LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ > 870000000UL)
#error "Single-channel frequency must be inside EU868"
#endif
#if (LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ % 100UL) != 0UL
#error "RadioLib LoRaWAN frequencies use 100 Hz resolution"
#endif
#if LORAWAN_SINGLE_CHANNEL_DR > 5U
#error "EU868 single-channel datarate must be DR0..DR5"
#endif
#if LORAWAN_ADR_ENABLED
#error "ADR must stay disabled in single-channel compatibility mode"
#endif
#endif

#if (LORAWAN_UPLINK_PORT < 1U) || (LORAWAN_UPLINK_PORT > 223U)
#error "LORAWAN_UPLINK_PORT must be in 1..223"
#endif

#if LORAWAN_UPLINK_INTERVAL_SECONDS == 0U
#error "LORAWAN_UPLINK_INTERVAL_SECONDS must be greater than zero"
#endif

#if (LORAWAN_CONFIRMED_UPLINK != 0) && (LORAWAN_CONFIRMED_UPLINK != 1)
#error "LORAWAN_CONFIRMED_UPLINK must be 0 or 1"
#endif

#if (LORAWAN_ADR_ENABLED != 0) && (LORAWAN_ADR_ENABLED != 1)
#error "LORAWAN_ADR_ENABLED must be 0 or 1"
#endif

#if (LORAWAN_TX_POWER_DBM < -9) || (LORAWAN_TX_POWER_DBM > 22)
#error "LORAWAN_TX_POWER_DBM exceeds the LR1121 LoRa range"
#endif

#if (LORAWAN_CLASS_C_POLL_INTERVAL_MS == 0U) ||                                \
    (LORAWAN_CLASS_C_POLL_INTERVAL_MS > 1000U)
#error "LORAWAN_CLASS_C_POLL_INTERVAL_MS must be in 1..1000"
#endif

#if (LORAWAN_SAFE_TEXT_MAX_BYTES == 0U) ||                                      \
    (LORAWAN_SAFE_TEXT_MAX_BYTES > RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE)
#error "LORAWAN_SAFE_TEXT_MAX_BYTES must fit the RadioLib payload limit"
#endif

#if LORAWAN_DISPLAY_MIN_INTERVAL_MS < 1000U
#error "LORAWAN_DISPLAY_MIN_INTERVAL_MS must be at least 1000"
#endif

#if (LORAWAN_JOIN_MAX_ATTEMPTS == 0U) ||                                       \
    (LORAWAN_CLASS_C_MAX_ATTEMPTS == 0U) ||                                    \
    (LORAWAN_RESTORED_SESSION_CHECK_ATTEMPTS == 0U)
#error "Join, Class C, and restored-session attempt counts must be non-zero"
#endif

#if (LORAWAN_JOIN_BACKOFF_BASE_SECONDS == 0U) ||                               \
    (LORAWAN_JOIN_BACKOFF_MAX_SECONDS < LORAWAN_JOIN_BACKOFF_BASE_SECONDS) ||  \
    (LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS == 0U) ||                             \
    (LORAWAN_CLASS_C_RETRY_SECONDS == 0U) ||                                   \
    (LORAWAN_SEND_FAILURES_BEFORE_BACKOFF == 0U) ||                            \
    (LORAWAN_RADIO_INIT_RETRY_SECONDS == 0U) ||                                \
    (LORAWAN_PERSIST_RETRY_MS == 0U)
#error "Retry, cooldown, and failure-threshold values must be non-zero"
#endif

#if (LORAWAN_FCNT_UP_RESERVATION_WINDOW < 2U) ||                               \
    (LORAWAN_FCNT_UP_RESERVATION_WINDOW > 16384U)
#error "LORAWAN_FCNT_UP_RESERVATION_WINDOW must be in 2..16384"
#endif
