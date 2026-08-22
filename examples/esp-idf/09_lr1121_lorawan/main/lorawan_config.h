/*
 * User-editable LoRaWAN provisioning and deployment policy.
 *
 * Keep secret values in lorawan_secrets.h. This public header contains only
 * defaults, region/channel selection, retry timing, and compile-time checks.
 */
#pragma once

#include <stdint.h>

#include <RadioLib.h>

/*
 * Put provisioned OTAA identity values in lorawan_secrets.h. That local file
 * is ignored by version control; lorawan_secrets.h.example is the template.
 * Keys are never logged.
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
#define LORAWAN_APP_KEY_BYTES { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#endif

#ifndef LORAWAN_NWK_KEY_BYTES
#define LORAWAN_NWK_KEY_BYTES { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#endif

/* LoRaWAN MAC version and regional channel plan. */
/* 1 selects LoRaWAN 1.1 (AppKey + NwkKey); 0 selects 1.0.x (AppKey). */
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

/*
 * EU868 is an example selection, not a universal board default. Change it to
 * the region configured in the network server and permitted at deployment.
 */
#define LORAWAN_REGION_SELECTION LORAWAN_REGION_EU868

/*
 * Dynamic plans use 0. Fixed plans require an explicit sub-band:
 * US915/AU915 use 1..8; CN470 uses 1..12.
 */
#define LORAWAN_SUB_BAND 0U

/*
 * The target gateway used by the known-working Starter listens on one EU868
 * uplink channel. This mode is intentionally explicit because a single-channel
 * gateway is not a standards-compliant public LoRaWAN deployment. Set it to 0
 * when using a normal multi-channel EU868 gateway.
 */
#define LORAWAN_SINGLE_CHANNEL_MODE         0
#define LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ 868100000UL
#define LORAWAN_SINGLE_CHANNEL_DR           3U

/* Application traffic and radio policy. */
#define LORAWAN_UPLINK_PORT             1U
#define LORAWAN_COMMAND_PORT            11U
#define LORAWAN_UPLINK_INTERVAL_SECONDS 60U
#define LORAWAN_CONFIRMED_UPLINK        0
#define LORAWAN_ADR_ENABLED             0
#define LORAWAN_TX_POWER_DBM            16

/*
 * Keep RadioLib's regional timing defaults. The application checks
 * timeUntilUplink() before each transmission instead of overriding the
 * regional duty-cycle or dwell-time policy.
 */

/* Bounded retry and recovery timing. */
#define LORAWAN_JOIN_MAX_ATTEMPTS            5U
#define LORAWAN_JOIN_BACKOFF_BASE_SECONDS    15U
#define LORAWAN_JOIN_BACKOFF_MAX_SECONDS     300U
#define LORAWAN_JOIN_BATCH_COOLDOWN_SECONDS  900U
#define LORAWAN_SEND_FAILURES_BEFORE_REJOIN  3U
#define LORAWAN_SEND_RECOVERY_SECONDS        300U
#define LORAWAN_RADIO_INIT_RETRY_SECONDS     60U
#define LORAWAN_PERSISTENCE_RETRY_SECONDS    60U

/* A channel-policy change discards only sess_sc and retains OTAA nonce. */

/* End of user configuration. */

/* Derived names and compile-time validation below this point. */
#if LORAWAN_USE_VERSION_1_1
#define LORAWAN_MAC_VERSION_NAME "1.1.0"
#else
#define LORAWAN_MAC_VERSION_NAME "1.0.4"
#endif

/* RadioLib 7.7.1 implements the RP002 Regional Parameters 1.0.4 tables. */
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

#if (LORAWAN_REGION_SELECTION == LORAWAN_REGION_US915) ||                       \
    (LORAWAN_REGION_SELECTION == LORAWAN_REGION_AU915)
#if (LORAWAN_SUB_BAND < 1U) || (LORAWAN_SUB_BAND > 8U)
#error "US915 and AU915 require LORAWAN_SUB_BAND in the range 1..8"
#endif
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_CN470
#if (LORAWAN_SUB_BAND < 1U) || (LORAWAN_SUB_BAND > 12U)
#error "CN470 requires LORAWAN_SUB_BAND in the range 1..12"
#endif
#elif LORAWAN_SUB_BAND != 0U
#error "This dynamic LoRaWAN region requires LORAWAN_SUB_BAND 0"
#endif

#if (LORAWAN_SINGLE_CHANNEL_MODE != 0) && \
    (LORAWAN_SINGLE_CHANNEL_MODE != 1)
#error "LORAWAN_SINGLE_CHANNEL_MODE must be 0 or 1"
#endif

#if LORAWAN_SINGLE_CHANNEL_MODE
#if LORAWAN_REGION_SELECTION != LORAWAN_REGION_EU868
#error "The configured single-channel compatibility mode supports EU868 only"
#endif
#if (LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ < 863000000UL) || \
    (LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ > 870000000UL)
#error "LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ must be inside EU868"
#endif
#if (LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ % 100UL) != 0UL
#error "RadioLib LoRaWAN frequencies use 100 Hz resolution"
#endif
#if LORAWAN_SINGLE_CHANNEL_DR > 5U
#error "LORAWAN_SINGLE_CHANNEL_DR must be in the EU868 DR0..DR5 range"
#endif
#if LORAWAN_ADR_ENABLED
#error "ADR must remain disabled in single-channel compatibility mode"
#endif
#endif

#if (LORAWAN_UPLINK_PORT < 1U) || (LORAWAN_UPLINK_PORT > 223U)
#error "LORAWAN_UPLINK_PORT must be in the application range 1..223"
#endif

#if (LORAWAN_COMMAND_PORT < 1U) || (LORAWAN_COMMAND_PORT > 223U)
#error "LORAWAN_COMMAND_PORT must be in the application range 1..223"
#endif

#if LORAWAN_UPLINK_INTERVAL_SECONDS == 0U
#error "LORAWAN_UPLINK_INTERVAL_SECONDS must be greater than zero"
#endif

#if (LORAWAN_TX_POWER_DBM < -9) || (LORAWAN_TX_POWER_DBM > 22)
#error "LORAWAN_TX_POWER_DBM exceeds the LR1121 LoRa output range"
#endif

#if LORAWAN_JOIN_MAX_ATTEMPTS == 0U
#error "LORAWAN_JOIN_MAX_ATTEMPTS must be greater than zero"
#endif

#if (LORAWAN_USE_VERSION_1_1 != 0) && (LORAWAN_USE_VERSION_1_1 != 1)
#error "LORAWAN_USE_VERSION_1_1 must be 0 or 1"
#endif

/* All other LoRaWAN NVS maintenance remains explicit. */
