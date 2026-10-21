#pragma once

#include <stdint.h>

#include <RadioLib.h>

/*
 * Put deployment credentials in lorawan_secrets.h. That file is ignored.
 * The all-zero defaults keep secrets out of this example and force the
 * CONFIG REQUIRED state before any radio transmission or multicast receive.
 */
#if __has_include("lorawan_secrets.h")
#include "lorawan_secrets.h"
#endif

#ifndef LORAWAN_JOIN_EUI
#define LORAWAN_JOIN_EUI UINT64_C(0x0000000000000000)
#endif
#ifndef LORAWAN_DEV_EUI
#define LORAWAN_DEV_EUI UINT64_C(0x0000000000000000)
#endif
#ifndef LORAWAN_APP_KEY_BYTES
#define LORAWAN_APP_KEY_BYTES \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif
#ifndef LORAWAN_NWK_KEY_BYTES
#define LORAWAN_NWK_KEY_BYTES \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif

/* Verified unicast OTAA baseline. */
#define LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ 868100000UL
#define LORAWAN_SINGLE_CHANNEL_DR           3U
#define LORAWAN_SUB_BAND                    0U
#define LORAWAN_ADR_ENABLED                 0U
#define LORAWAN_INITIAL_POWER_DBM           10
#define LORAWAN_UPLINK_PORT                 1U
#define LORAWAN_UNICAST_DOWNLINK_PORT       2U
#define LORAWAN_UPLINK_INTERVAL_SECONDS     60UL
#define LORAWAN_JOIN_MAX_ATTEMPTS           5U
#define LORAWAN_JOIN_RETRY_SECONDS          15UL

/* Static multicast group. RadioLib 7.7.1 supports Class C only. */
#ifndef MULTICAST_GROUP_ID
#define MULTICAST_GROUP_ID                  0U
#endif
#ifndef MULTICAST_MC_ADDR
#define MULTICAST_MC_ADDR                   0x00000000UL
#endif
#ifndef MULTICAST_MC_APP_S_KEY_BYTES
#define MULTICAST_MC_APP_S_KEY_BYTES \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif
#ifndef MULTICAST_MC_NWK_S_KEY_BYTES
#define MULTICAST_MC_NWK_S_KEY_BYTES \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif
#define MULTICAST_DEVICE_CLASS              RADIOLIB_LORAWAN_CLASS_C
#define MULTICAST_FREQUENCY_HZ              869525000UL
#define MULTICAST_DATARATE                  0U
#define MULTICAST_DOWNLINK_PORT             10U
#define MULTICAST_MC_FCNT_MAX               UINT32_MAX

/* Increment when replacing the server-side group while reusing McAddr. */
#define MULTICAST_CONFIG_GENERATION         1UL

/* Explicit maintenance switch. A clear boot stops before radio startup. */
#define MULTICAST_CLEAR_STATE_ON_BOOT       0U

#define MULTICAST_CLASS_C_POLL_MS           20U
#define MULTICAST_RX_ERROR_RESTART_COUNT    3U
#define MULTICAST_ERROR_BACKOFF_SECONDS     5UL
#define MULTICAST_MAX_BACKOFF_SECONDS       300UL
#define MULTICAST_LOG_HEX_ENABLED           1U
#define MULTICAST_LOG_TEXT_ENABLED          1U
#define MULTICAST_LOG_SAFE_TEXT_LIMIT       96U
#define MULTICAST_DISPLAY_MIN_INTERVAL_MS   5000U
#define MULTICAST_DISPLAY_TASK_STACK        8192U
#define MULTICAST_DISPLAY_TASK_PRIORITY     3U

static_assert(LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ == 868100000UL,
              "Example 12 baseline frequency must remain 868.1 MHz");
static_assert(LORAWAN_SINGLE_CHANNEL_DR == 3U,
              "Example 12 baseline datarate must remain DR3");
static_assert(MULTICAST_DISPLAY_TASK_STACK >= 8192U,
              "LVGL e-paper display worker requires at least 8192 bytes");
static_assert(LORAWAN_SUB_BAND == 0U,
              "EU868 dynamic plans use sub-band 0");
static_assert(LORAWAN_ADR_ENABLED == 0U,
              "The verified single-channel baseline requires ADR disabled");
static_assert(MULTICAST_GROUP_ID < RADIOLIB_LORAWAN_MAX_NUM_MC_GROUPS,
              "RadioLib 7.7.1 provides multicast slots 0..3");
static_assert(MULTICAST_DEVICE_CLASS == RADIOLIB_LORAWAN_CLASS_C,
              "RadioLib 7.7.1 multicast supports Class C only");
static_assert(MULTICAST_FREQUENCY_HZ >= 863000000UL &&
              MULTICAST_FREQUENCY_HZ <= 870000000UL,
              "Multicast frequency must be inside EU868");
static_assert((MULTICAST_FREQUENCY_HZ % 100UL) == 0UL,
              "RadioLib LoRaWAN frequencies use 100 Hz resolution");
static_assert(MULTICAST_DATARATE <= 5U,
              "EU868 LoRa multicast DR must be DR0..DR5");
static_assert(MULTICAST_DOWNLINK_PORT > 0U &&
              MULTICAST_DOWNLINK_PORT < 224U,
              "Use an application FPort in the range 1..223");
static_assert(LORAWAN_UPLINK_PORT > 0U && LORAWAN_UPLINK_PORT < 224U,
              "Use an application FPort in the range 1..223");
static_assert(MULTICAST_LOG_HEX_ENABLED <= 1U,
              "HEX logging switch must be 0 or 1");
static_assert(MULTICAST_LOG_TEXT_ENABLED <= 1U,
              "Text logging switch must be 0 or 1");
static_assert(MULTICAST_LOG_SAFE_TEXT_LIMIT > 0U &&
              MULTICAST_LOG_SAFE_TEXT_LIMIT <=
                  RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE,
              "Safe text log limit must fit the downlink buffer");


