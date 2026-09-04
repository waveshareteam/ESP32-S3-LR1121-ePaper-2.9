/*
 * Stores the exact RadioLib 7.7.1 persistence buffers in NVS.
 * A separate, non-secret policy marker prevents a single-channel session
 * from restoring its stale channel mask after multichannel mode is enabled.
 */
#include "lorawan_session.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "lorawan_config.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "lorawan_nvs";

// Keep the two native buffer names compatible with the Starter firmware.
static const char *const LORAWAN_NVS_NAMESPACE = "lorawan";
static const char *const LORAWAN_NVS_NONCES_KEY = "nonce";
static const char *const LORAWAN_NVS_SESSION_KEY = "sess_sc";
static const char *const LORAWAN_NVS_CHANNEL_POLICY_KEY = "chan_policy";

// One handle remains open for the lifetime of the application task.
static nvs_handle_t s_nvs_handle;
static bool s_storage_open;

// Fail loudly if a RadioLib upgrade changes either serialized layout.
static_assert(RADIOLIB_LORAWAN_NONCES_BUF_SIZE == 14,
              "RadioLib 7.7.1 nonce layout changed");
static_assert(RADIOLIB_LORAWAN_SESSION_BUF_SIZE == 302,
              "RadioLib 7.7.1 session layout changed");

// Increment this schema when the packed marker layout changes.
static constexpr uint8_t LORAWAN_CHANNEL_POLICY_SCHEMA = 1U;

/*
 * Pack only channel-plan inputs into a stable 64-bit value. Frequency is
 * stored at RadioLib's 100 Hz resolution and matters only in explicit
 * single-channel compatibility mode.
 */
static uint64_t configured_channel_policy_id(void)
{
    uint64_t policy =
        (UINT64_C(0xA7) << 56) |
        ((uint64_t)LORAWAN_CHANNEL_POLICY_SCHEMA << 48) |
        ((uint64_t)LORAWAN_REGION_SELECTION << 40) |
        ((uint64_t)LORAWAN_SUB_BAND << 36) |
        ((uint64_t)LORAWAN_SINGLE_CHANNEL_MODE << 35);

#if LORAWAN_SINGLE_CHANNEL_MODE
    policy |= ((uint64_t)LORAWAN_SINGLE_CHANNEL_DR << 31);
    policy |= (uint64_t)(LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ / 100UL);
#endif
    return policy;
}

static esp_err_t storage_is_ready(void)
{
    return s_storage_open ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/*
 * RadioLib includes channel masks and flags in sess_sc. When the firmware
 * channel policy changes, remove only that session and keep nonce history so
 * the following OTAA join remains compliant with DevNonce requirements.
 */
/* Clears only stale RadioLib OTAA buffers after an explicit rejection. */
static esp_err_t clear_incompatible_otaa_cache(void)
{
    const char *const keys[] = {
        LORAWAN_NVS_NONCES_KEY,
        LORAWAN_NVS_SESSION_KEY,
        LORAWAN_NVS_CHANNEL_POLICY_KEY,
    };
    bool erased = false;
    for (const char *key : keys) {
        const esp_err_t err = nvs_erase_key(s_nvs_handle, key);
        if (err == ESP_OK) {
            erased = true;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }
    return erased ? nvs_commit(s_nvs_handle) : ESP_OK;
}

static esp_err_t prepare_session_for_channel_policy(
    lorawan_restore_result_t *result,
    bool *restore_session)
{
    const uint64_t current_policy = configured_channel_policy_id();
    uint64_t saved_policy = 0;
    const esp_err_t read_err = nvs_get_u64(
        s_nvs_handle, LORAWAN_NVS_CHANNEL_POLICY_KEY, &saved_policy);

    if (read_err == ESP_OK && saved_policy == current_policy) {
        *restore_session = true;
        return ESP_OK;
    }
    if (read_err != ESP_OK &&
        read_err != ESP_ERR_NVS_NOT_FOUND &&
        read_err != ESP_ERR_NVS_TYPE_MISMATCH) {
        return read_err;
    }

    const esp_err_t session_erase =
        nvs_erase_key(s_nvs_handle, LORAWAN_NVS_SESSION_KEY);
    if (session_erase != ESP_OK && session_erase != ESP_ERR_NVS_NOT_FOUND) {
        return session_erase;
    }

    const esp_err_t marker_erase =
        nvs_erase_key(s_nvs_handle, LORAWAN_NVS_CHANNEL_POLICY_KEY);
    if (marker_erase != ESP_OK && marker_erase != ESP_ERR_NVS_NOT_FOUND) {
        return marker_erase;
    }

    esp_err_t err = nvs_set_u64(
        s_nvs_handle, LORAWAN_NVS_CHANNEL_POLICY_KEY, current_policy);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    if (err != ESP_OK) {
        return err;
    }

    *restore_session = false;
    result->session_discarded_for_policy = session_erase == ESP_OK;

    if (result->session_discarded_for_policy) {
        const char *reason = read_err == ESP_ERR_NVS_NOT_FOUND
                                 ? "missing"
                                 : (read_err == ESP_OK ? "changed"
                                                       : "invalid");
        ESP_LOGW(TAG,
                 "%s channel policy marker; saved session discarded, "
                 "nonce retained for a fresh OTAA join",
                 reason);
    } else {
        ESP_LOGI(TAG, "channel policy marker initialized");
    }
    return ESP_OK;
}

esp_err_t lorawan_storage_init(void)
{
    if (s_storage_open) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGE(TAG,
                 "NVS requires destructive reinitialization (%s); "
                 "automatic erase is disabled to protect LoRaWAN counters",
                 esp_err_to_name(err));
        return err;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_open(LORAWAN_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err == ESP_OK) {
        s_storage_open = true;
    }
    return err;
}

esp_err_t lorawan_session_restore(LoRaWANNode &node,
                                  lorawan_restore_result_t *result)
{
    if (result == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = {};

    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Nonces must be restored before the session because RadioLib validates
     * the session signature against the current nonce/base state.
     */
    uint8_t nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE] = {};
    size_t size = sizeof(nonces);
    err = nvs_get_blob(s_nvs_handle, LORAWAN_NVS_NONCES_KEY,
                       nonces, &size);
    if (err == ESP_OK && size == sizeof(nonces)) {
        const int16_t state = node.setBufferNonces(nonces);
        if (state == RADIOLIB_ERR_NONE) {
            result->nonces_restored = true;
            ESP_LOGI(TAG, "nonce state restored");
        } else {
            ESP_LOGW(TAG,
                     "RadioLib rejected saved nonce state: %d; "
                     "continuing with a new OTAA attempt",
                     state);
                    if (state == RADIOLIB_ERR_NONCES_DISCARDED) {
                err = clear_incompatible_otaa_cache();
                if (err != ESP_OK) {
                    return err;
                }
                result->incompatible_otaa_cache_cleared = true;
                ESP_LOGW(TAG,
                         "discarded OTAA cache cleared; rejoining with "
                         "current JoinEUI");
            }
        }
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "saved nonce state has size %u, expected %u",
                 (unsigned)size, (unsigned)sizeof(nonces));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to restore nonce state: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "no saved nonce state");
    }

    bool restore_session = false;
    err = prepare_session_for_channel_policy(result, &restore_session);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "channel policy storage check failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    if (!restore_session) {
        return ESP_OK;
    }

    // A rejected or missing session is non-fatal; OTAA will create a new one.
    uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};
    size = sizeof(session);
    err = nvs_get_blob(s_nvs_handle, LORAWAN_NVS_SESSION_KEY,
                       session, &size);
    if (err == ESP_OK && size == sizeof(session)) {
        const int16_t state = node.setBufferSession(session);
        if (state == RADIOLIB_ERR_NONE) {
            result->session_restored = true;
            ESP_LOGI(TAG, "session state restored");
        } else {
            ESP_LOGW(TAG,
                     "RadioLib rejected saved session state: %d; "
                     "continuing with OTAA",
                     state);
        }
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "saved session state has size %u, expected %u",
                 (unsigned)size, (unsigned)sizeof(session));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to restore session state: %s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "no saved session state");
    }

    return ESP_OK;
}

/*
 * Commit DevNonce progress after every activateOTAA() result, including
 * failed joins, before another JoinRequest can be transmitted.
 */
esp_err_t lorawan_session_save_nonces(LoRaWANNode &node)
{
    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(s_nvs_handle, LORAWAN_NVS_NONCES_KEY,
                       node.getBufferNonces(),
                       RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    return err;
}

// Commit both native buffers and their policy as one logical update.
esp_err_t lorawan_session_save(LoRaWANNode &node)
{
    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(s_nvs_handle, LORAWAN_NVS_NONCES_KEY,
                       node.getBufferNonces(),
                       RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (err == ESP_OK) {
        err = nvs_set_blob(s_nvs_handle, LORAWAN_NVS_SESSION_KEY,
                           node.getBufferSession(),
                           RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    }
    if (err == ESP_OK) {
        err = nvs_set_u64(s_nvs_handle,
                          LORAWAN_NVS_CHANNEL_POLICY_KEY,
                          configured_channel_policy_id());
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "nonce and session state saved");
    }
    return err;
}

// Controlled rejoin erases sess_sc only; nonce and policy marker survive.
esp_err_t lorawan_session_clear_saved_session(void)
{
    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(s_nvs_handle, LORAWAN_NVS_SESSION_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    return err;
}
