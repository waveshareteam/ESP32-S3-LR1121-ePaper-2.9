/**
 * @file lorawan_session.cpp
 * @brief Implements monotonic, crash-safe LoRaWAN state storage in NVS.
 */

#include "lorawan_session.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "lorawan_nvs";

static const char *const LORAWAN_NVS_NAMESPACE = "lorawan";
static const char *const LORAWAN_NVS_NONCES_KEY = "nonce";
static const char *const LORAWAN_NVS_SESSION_KEY = "sess_sc";

// The distinct name avoids the ESP-IDF nvs_handle type alias collision.
static nvs_handle_t s_nvs_handle;
static bool s_storage_open;
static bool s_fcnt_highwater_valid;
static uint32_t s_fcnt_highwater;

// The offsets below are version-specific and must not silently drift.
static_assert(RADIOLIB_LORAWAN_NONCES_BUF_SIZE == 14,
              "RadioLib 7.7.1 nonce layout changed");
static_assert(RADIOLIB_LORAWAN_SESSION_BUF_SIZE == 302,
              "RadioLib 7.7.1 session layout changed");

static esp_err_t storage_is_ready(void)
{
    return s_storage_open ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static uint32_t session_fcnt_up(const uint8_t *session)
{
    const uint8_t *value =
        &session[RADIOLIB_LORAWAN_SESSION_FCNT_UP];
    // RadioLib 7.7.1 serializes this field least-significant byte first.
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static esp_err_t save_session_snapshot(
    LoRaWANNode &node, const uint8_t *session_snapshot,
    size_t session_snapshot_size, bool is_reservation)
{
    if (session_snapshot == nullptr ||
        session_snapshot_size != RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    const uint32_t candidate_fcnt = session_fcnt_up(session_snapshot);
    // A rollback could reuse an uplink counter after an unexpected reset.
    if (s_fcnt_highwater_valid &&
        candidate_fcnt < s_fcnt_highwater) {
        ESP_LOGE(TAG,
                 "refusing to lower persisted FCntUp high-water mark "
                 "from %" PRIu32 " to %" PRIu32,
                 s_fcnt_highwater, candidate_fcnt);
        return ESP_ERR_INVALID_STATE;
    }

    err = nvs_set_blob(s_nvs_handle, LORAWAN_NVS_NONCES_KEY,
                       node.getBufferNonces(),
                       RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (err == ESP_OK) {
        err = nvs_set_blob(s_nvs_handle, LORAWAN_NVS_SESSION_KEY,
                           session_snapshot, session_snapshot_size);
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    if (err == ESP_OK) {
        s_fcnt_highwater = candidate_fcnt;
        s_fcnt_highwater_valid = true;
        if (is_reservation) {
            ESP_LOGI(TAG,
                     "uplink FCntUp high-water mark saved: %" PRIu32,
                     candidate_fcnt);
        } else {
            ESP_LOGI(TAG,
                     "nonce and session state saved; FCntUp "
                     "high-water mark=%" PRIu32,
                     candidate_fcnt);
        }
    }
    return err;
}

/* Removes only rejected OTAA buffers; it never erases the NVS partition. */
static esp_err_t clear_incompatible_otaa_cache(void)
{
    const char *const keys[] = {
        LORAWAN_NVS_NONCES_KEY,
        LORAWAN_NVS_SESSION_KEY,
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
    if (erased) {
        const esp_err_t err = nvs_commit(s_nvs_handle);
        if (err != ESP_OK) {
            return err;
        }
    }
    s_fcnt_highwater = 0;
    s_fcnt_highwater_valid = false;
    return ESP_OK;
}

esp_err_t lorawan_storage_init(void)
{
    if (s_storage_open) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    // Automatic erase would discard nonces and frame-counter protection.
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

    // A session is accepted only after its associated nonce state is valid.
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
                ESP_LOGW(TAG,
                         "discarded OTAA cache cleared; rejoining with "
                         "current JoinEUI");
            }
        }
    } else if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH ||
               err == ESP_ERR_NVS_TYPE_MISMATCH) {
        ESP_LOGW(TAG,
                 "saved nonce state is invalid: %s, size %u, expected %u",
                 esp_err_to_name(err), (unsigned)size,
                 (unsigned)sizeof(nonces));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "failed to read nonce state: %s",
                 esp_err_to_name(err));
        return err;
    } else {
        ESP_LOGI(TAG, "no saved nonce state");
    }

    if (!result->nonces_restored) {
        ESP_LOGW(TAG,
                 "session restore skipped because nonce state is unavailable");
        err = lorawan_session_clear_saved_session();
        return err;
    }

    uint8_t session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};
    size = sizeof(session);
    err = nvs_get_blob(s_nvs_handle, LORAWAN_NVS_SESSION_KEY,
                       session, &size);
    if (err == ESP_OK && size == sizeof(session)) {
        const int16_t state = node.setBufferSession(session);
        if (state == RADIOLIB_ERR_NONE) {
            result->session_restored = true;
            s_fcnt_highwater = session_fcnt_up(session);
            s_fcnt_highwater_valid = true;
            ESP_LOGI(TAG,
                     "session state restored at FCntUp high-water mark %"
                     PRIu32,
                     s_fcnt_highwater);
        } else {
            ESP_LOGW(TAG,
                     "RadioLib rejected saved session state: %d; "
                     "continuing with OTAA",
                     state);
            err = lorawan_session_clear_saved_session();
            if (err != ESP_OK) {
                return err;
            }
        }
    } else if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH ||
               err == ESP_ERR_NVS_TYPE_MISMATCH) {
        ESP_LOGW(TAG,
                 "saved session state is invalid: %s, size %u, expected %u",
                 esp_err_to_name(err), (unsigned)size,
                 (unsigned)sizeof(session));
        err = lorawan_session_clear_saved_session();
        if (err != ESP_OK) {
            return err;
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "failed to read session state: %s",
                 esp_err_to_name(err));
        return err;
    } else {
        ESP_LOGI(TAG, "no saved session state");
    }

    return ESP_OK;
}

esp_err_t lorawan_session_save_nonces(LoRaWANNode &node)
{
    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    // JoinRequest advances DevNonce even when activation ultimately fails.
    err = nvs_set_blob(s_nvs_handle, LORAWAN_NVS_NONCES_KEY,
                       node.getBufferNonces(),
                       RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    return err;
}

esp_err_t lorawan_session_save(
    LoRaWANNode &node, const uint8_t *session_snapshot,
    size_t session_snapshot_size)
{
    return save_session_snapshot(node, session_snapshot,
                                 session_snapshot_size, false);
}

esp_err_t lorawan_session_save_uplink_reservation(
    LoRaWANNode &node, const uint8_t *reserved_session,
    size_t reserved_session_size)
{
    return save_session_snapshot(node, reserved_session,
                                 reserved_session_size, true);
}

esp_err_t lorawan_session_clear_saved_session(void)
{
    esp_err_t err = storage_is_ready();
    if (err != ESP_OK) {
        return err;
    }

    // Keep the nonce key so the next OTAA request cannot reuse DevNonce.
    err = nvs_erase_key(s_nvs_handle, LORAWAN_NVS_SESSION_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_fcnt_highwater = 0;
        s_fcnt_highwater_valid = false;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs_handle);
    }
    if (err == ESP_OK) {
        s_fcnt_highwater = 0;
        s_fcnt_highwater_valid = false;
    }
    return err;
}
