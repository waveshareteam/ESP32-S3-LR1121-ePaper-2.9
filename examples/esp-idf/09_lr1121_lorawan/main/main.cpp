/*
 * LR1121 LoRaWAN example entry point.
 *
 * This file keeps only provisioning validation and the required startup
 * sequence. Join retries, uplinks, downlinks, and recovery live in the
 * dedicated runtime module.
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include <RadioLib.h>

#include "board_lorawan_radio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lorawan_config.h"
#include "lorawan_node.h"
#include "lorawan_runtime.h"
#include "lorawan_session.h"
#include "lorawan_ui.h"

static const char *TAG = "lr1121_lorawan";

// Root keys stay private to startup code and are never written to logs.
static const uint8_t s_app_key[16] = LORAWAN_APP_KEY_BYTES;
static const uint8_t s_nwk_key[16] = LORAWAN_NWK_KEY_BYTES;

// Detect the all-zero and all-0xFF placeholder keys before any radio access.
static bool key_is_uniform(const uint8_t *key, uint8_t value)
{
    for (size_t i = 0; i < 16U; ++i) {
        if (key[i] != value) {
            return false;
        }
    }
    return true;
}

// Reject incomplete provisioning instead of repeatedly sending invalid joins.
static bool configuration_is_valid(const LoRaWANBand_t &band)
{
    bool valid = true;

    if (LORAWAN_JOIN_EUI == UINT64_MAX) {
        ESP_LOGE(TAG, "JoinEUI is still the placeholder");
        valid = false;
    }
    if (LORAWAN_DEV_EUI == UINT64_MAX || LORAWAN_DEV_EUI == 0U) {
        ESP_LOGE(TAG, "DevEUI is missing or still the placeholder");
        valid = false;
    }
    if (key_is_uniform(s_app_key, 0xFF) ||
        key_is_uniform(s_app_key, 0x00)) {
        ESP_LOGE(TAG, "AppKey is missing or still a placeholder");
        valid = false;
    }
    if (LORAWAN_USE_VERSION_1_1 &&
        (key_is_uniform(s_nwk_key, 0xFF) ||
         key_is_uniform(s_nwk_key, 0x00))) {
        ESP_LOGE(TAG, "NwkKey is required for LoRaWAN 1.1");
        valid = false;
    }
    // The regional plan is an additional limit on top of the LR1121 limit.
    if (LORAWAN_TX_POWER_DBM > band.powerMax) {
        ESP_LOGE(TAG,
                 "configured TX power %d dBm exceeds %s plan maximum %d dBm",
                 LORAWAN_TX_POWER_DBM, LORAWAN_REGION_NAME,
                 (int)band.powerMax);
        valid = false;
    }

    if (!valid) {
        ESP_LOGE(TAG,
                 "edit main/lorawan_secrets.h and main/lorawan_config.h; "
                 "no OTAA request will be sent");
    }
    return valid;
}

// Print non-secret deployment settings so field logs identify the firmware mode.
static void log_configuration(const LoRaWANBand_t &band)
{
    ESP_LOGI(TAG, "LR1121 LoRaWAN example, RadioLib %u.%u.%u",
             (unsigned)RADIOLIB_VERSION_MAJOR,
             (unsigned)RADIOLIB_VERSION_MINOR,
             (unsigned)RADIOLIB_VERSION_PATCH);
    ESP_LOGI(TAG,
             "OTAA LoRaWAN %s reg-params=%s region=%s sub-band=%u "
             "port=%u interval=%us "
             "ADR=%d confirmed=%d TX=%ddBm timing=RadioLib-default",
             LORAWAN_MAC_VERSION_NAME,
             LORAWAN_REG_PARAMS_REVISION_NAME,
             LORAWAN_REGION_NAME, (unsigned)LORAWAN_SUB_BAND,
             (unsigned)LORAWAN_UPLINK_PORT,
             (unsigned)LORAWAN_UPLINK_INTERVAL_SECONDS,
             LORAWAN_ADR_ENABLED, LORAWAN_CONFIRMED_UPLINK,
             LORAWAN_TX_POWER_DBM);
#if LORAWAN_SINGLE_CHANNEL_MODE
    ESP_LOGW(TAG,
             "single-channel gateway compatibility: %" PRIu32 " Hz DR%u, "
             "ADR disabled; use only with the configured gateway",
             (uint32_t)LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ,
             (unsigned)LORAWAN_SINGLE_CHANNEL_DR);
#elif LORAWAN_REGION_SELECTION == LORAWAN_REGION_EU868
    ESP_LOGI(TAG,
             "standard EU868 initial uplink channels: "
             "868.1, 868.3, 868.5 MHz");
    ESP_LOGI(TAG,
             "additional EU868 channels require JoinAccept CFList or "
             "network MAC commands");
#endif
    ESP_LOGI(TAG,
             "LR1121 bootstrap frequency %.4f MHz; RadioLib selects the "
             "actual frequency for every LoRaWAN transmission",
             (double)lorawan_initial_frequency_mhz(band));
    ESP_LOGI(TAG,
             "NVS state format: namespace=lorawan "
             "keys=nonce,sess_sc,chan_policy");
}

// Retry hardware initialization at a bounded rate; no OTAA traffic is sent here.
static LR1121 *initialize_radio(float frequency_mhz, LoRaWANStatusUi &ui)
{
    LR1121 *radio = nullptr;

    while (board_lorawan_radio_init(
               frequency_mhz, LORAWAN_TX_POWER_DBM, &radio) != ESP_OK) {
        ESP_LOGE(TAG, "LR1121 init failed; retrying in %u seconds",
                 (unsigned)LORAWAN_RADIO_INIT_RETRY_SECONDS);
        ui.update("RADIO RETRY", "LR1121 init", ESP_FAIL);
        lorawan_delay_ms(
            lorawan_seconds_to_ms(LORAWAN_RADIO_INIT_RETRY_SECONDS));
    }
    return radio;
}

extern "C" void app_main(void)
{
    // The band object has static lifetime because RadioLib stores its address.
    const LoRaWANBand_t &band = lorawan_configured_band();
    LoRaWANStatusUi ui;

    log_configuration(band);

    const bool valid = configuration_is_valid(band);
    ui.update(valid ? "STARTING" : "CONFIG REQUIRED",
              valid ? "OTAA setup" : "edit secrets",
              valid ? ESP_OK : ESP_ERR_INVALID_STATE);
    if (!valid) {
        lorawan_wait_for_configuration();
    }

    // Open NVS before constructing the node so saved nonces can be restored.
    esp_err_t err = lorawan_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s",
                 esp_err_to_name(err));
        ui.update("NVS ERROR", esp_err_to_name(err), err);
        lorawan_wait_for_configuration();
    }

    // Reuse the board-managed SPI bus, RF switch, TCXO, and high-power PA path.
    LR1121 *radio = initialize_radio(
        lorawan_initial_frequency_mhz(band), ui);
    ConfiguredLoRaWANNode node(radio, &band, LORAWAN_SUB_BAND);

    /*
     * beginOTAA() must run before restoring RadioLib buffers because it binds
     * the in-memory state to the current identity, keys, mode, and region.
     */
    const int16_t begin_state = node.beginOTAA(
        LORAWAN_JOIN_EUI,
        LORAWAN_DEV_EUI,
        LORAWAN_USE_VERSION_1_1 ? s_nwk_key : nullptr,
        s_app_key);
    if (begin_state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "beginOTAA failed: %d (%s)",
                 begin_state, radiolib_state_name(begin_state));
        ui.update("OTAA ERROR",
                  radiolib_state_name(begin_state), begin_state);
        lorawan_wait_for_configuration();
    }

    // Restore nonce, validate channel policy, then restore a compatible session.
    lorawan_restore_result_t restored = {};
    err = lorawan_session_restore(node, &restored);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "restore LoRaWAN state failed: %s",
                 esp_err_to_name(err));
        ui.update("STATE ERROR", esp_err_to_name(err), err);
        lorawan_wait_for_configuration();
    }
    ESP_LOGI(TAG, "NVS restore: nonce=%d session=%d policy-discard=%d cache-reset=%d",
             restored.nonces_restored, restored.session_restored,
             restored.session_discarded_for_policy,
             restored.incompatible_otaa_cache_cleared);
    if (restored.incompatible_otaa_cache_cleared) {
        ui.update("OTAA CACHE RESET", "REJOINING", RADIOLIB_ERR_NONE);
    }
    if (restored.session_discarded_for_policy) {
        ESP_LOGW(TAG,
                 "channel policy changed: retained nonce and scheduled "
                 "one normal OTAA rejoin");
    }

    // This call owns the task for the lifetime of the Class A application.
    lorawan_runtime_run(node, *radio, ui);
}
