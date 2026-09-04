#include "multicast_store.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"

/*
 * NVS implementation for RadioLib unicast buffers and multicast replay state.
 * Secret group keys stay in provisioning configuration and are never copied
 * into this module's multicast record.
 */

namespace {

/* These names are persistent storage ABI; changing them starts a new record. */
constexpr char kNvsNamespace[] = "lorawan";
constexpr char kNonceKey[] = "nonce";
constexpr char kSessionKey[] = "sess_sc";
constexpr char kMulticastKey[] = "mc_state_v1";

/*
 * The packed field order is intentionally fixed at 32 bytes. Bytes in the
 * magic value spell "MC14" on the little-endian ESP32.
 */
constexpr uint32_t kMulticastMagic = UINT32_C(0x3431434D);
constexpr uint16_t kMulticastSchemaVersion = 1U;

struct MulticastBlob {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t structure_length;
    uint8_t group_id;
    uint8_t datarate;
    uint8_t counter_valid;
    uint8_t reserved;
    uint32_t config_generation;
    uint32_t mc_addr;
    uint32_t frequency_hz;
    uint32_t last_fcnt_down;
    uint32_t crc32;
};

static_assert(sizeof(MulticastBlob) == 32U,
              "Multicast NVS schema layout changed");
static_assert(offsetof(MulticastBlob, crc32) == 28U,
              "CRC must be the final multicast schema field");

nvs_handle_t s_store_handle = 0;
bool s_store_open = false;
SemaphoreHandle_t s_store_mutex = nullptr;
portMUX_TYPE s_store_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

/* Holds the NVS mutex for one complete read/validate or write/commit operation. */
class StoreGuard {
public:
    StoreGuard()
        : handle_(s_store_mutex),
          locked_(handle_ != nullptr &&
                  xSemaphoreTake(handle_, portMAX_DELAY) == pdTRUE)
    {
    }

    ~StoreGuard()
    {
        if (locked_) {
            xSemaphoreGive(handle_);
        }
    }

    bool locked() const { return locked_; }

private:
    SemaphoreHandle_t handle_;
    bool locked_;
};

/* Creates the mutex once even if initialization is requested concurrently. */
bool ensure_store_mutex()
{
    portENTER_CRITICAL(&s_store_mutex_init_lock);
    const bool already_created = s_store_mutex != nullptr;
    portEXIT_CRITICAL(&s_store_mutex_init_lock);
    if (already_created) {
        return true;
    }

    SemaphoreHandle_t candidate = xSemaphoreCreateMutex();
    if (candidate == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_store_mutex_init_lock);
    if (s_store_mutex == nullptr) {
        s_store_mutex = candidate;
        candidate = nullptr;
    }
    portEXIT_CRITICAL(&s_store_mutex_init_lock);

    if (candidate != nullptr) {
        vSemaphoreDelete(candidate);
    }
    return true;
}

MulticastStoreResult make_result(
    MulticastStoreStatus status,
    esp_err_t esp_error = ESP_OK,
    int16_t radiolib_status = RADIOLIB_ERR_NONE)
{
    return {status, esp_error, radiolib_status};
}

MulticastStoreResult not_initialized_result()
{
    return make_result(MulticastStoreStatus::kNotInitialized,
                       ESP_ERR_INVALID_STATE);
}

MulticastStoreResult lock_error_result()
{
    return make_result(MulticastStoreStatus::kLockError, ESP_ERR_TIMEOUT);
}

uint32_t crc32_ieee(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

/* The CRC covers every schema field except the trailing CRC itself. */
uint32_t multicast_blob_crc(const MulticastBlob &blob)
{
    return crc32_ieee(reinterpret_cast<const uint8_t *>(&blob),
                      offsetof(MulticastBlob, crc32));
}

bool config_is_valid(const MulticastStoreConfig &config)
{
    return config.group_id < RADIOLIB_LORAWAN_MAX_NUM_MC_GROUPS &&
           config.config_generation != 0U &&
           config.mc_addr != 0U &&
           config.frequency_hz != 0U;
}

MulticastBlob make_multicast_blob(
    const MulticastStoreConfig &config,
    bool counter_valid,
    uint32_t last_fcnt_down)
{
    MulticastBlob blob = {};
    blob.magic = kMulticastMagic;
    blob.schema_version = kMulticastSchemaVersion;
    blob.structure_length = sizeof(MulticastBlob);
    blob.group_id = config.group_id;
    blob.datarate = config.datarate;
    blob.counter_valid = counter_valid ? 1U : 0U;
    blob.reserved = 0U;
    blob.config_generation = config.config_generation;
    blob.mc_addr = config.mc_addr;
    blob.frequency_hz = config.frequency_hz;
    blob.last_fcnt_down = last_fcnt_down;
    blob.crc32 = multicast_blob_crc(blob);
    return blob;
}

MulticastStoreResult read_blob_strict_unlocked(
    const char *key,
    void *destination,
    size_t expected_length)
{
    size_t stored_length = 0U;
    esp_err_t error = nvs_get_blob(s_store_handle, key, nullptr,
                                   &stored_length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return make_result(MulticastStoreStatus::kNotFound, error);
    }
    if (error != ESP_OK) {
        return make_result(MulticastStoreStatus::kNvsError, error);
    }
    if (stored_length != expected_length) {
        return make_result(MulticastStoreStatus::kInvalidLength,
                           ESP_ERR_NVS_INVALID_LENGTH);
    }

    size_t read_length = expected_length;
    error = nvs_get_blob(s_store_handle, key, destination, &read_length);
    if (error == ESP_ERR_NVS_INVALID_LENGTH ||
        (error == ESP_OK && read_length != expected_length)) {
        return make_result(MulticastStoreStatus::kInvalidLength,
                           ESP_ERR_NVS_INVALID_LENGTH);
    }
    if (error != ESP_OK) {
        return make_result(MulticastStoreStatus::kNvsError, error);
    }
    return make_result(MulticastStoreStatus::kOk);
}

MulticastStoreResult close_after_write_error_unlocked(esp_err_t error)
{
    /*
     * NVS writes may already be durable. Closing only makes this wrapper fail
     * closed until reinitialized; it is not a rollback operation.
     */
    if (s_store_open) {
        nvs_close(s_store_handle);
        s_store_handle = 0;
        s_store_open = false;
    }
    return make_result(MulticastStoreStatus::kNvsError, error);
}

MulticastStoreResult commit_unlocked()
{
    const esp_err_t error = nvs_commit(s_store_handle);
    if (error != ESP_OK) {
        return close_after_write_error_unlocked(error);
    }
    return make_result(MulticastStoreStatus::kOk);
}

MulticastStoreResult write_multicast_blob_unlocked(const MulticastBlob &blob)
{
    const esp_err_t error = nvs_set_blob(s_store_handle, kMulticastKey,
                                         &blob, sizeof(blob));
    if (error != ESP_OK) {
        return close_after_write_error_unlocked(error);
    }
    return commit_unlocked();
}

/* Rejects old, damaged, or differently configured records before use. */
MulticastStoreResult validate_multicast_blob(
    const MulticastBlob &blob,
    const MulticastStoreConfig &config)
{
    if (blob.magic != kMulticastMagic) {
        return make_result(MulticastStoreStatus::kInvalidMagic);
    }
    if (blob.schema_version != kMulticastSchemaVersion) {
        return make_result(MulticastStoreStatus::kUnsupportedSchema);
    }
    if (blob.structure_length != sizeof(MulticastBlob)) {
        return make_result(MulticastStoreStatus::kInvalidLength);
    }
    if (multicast_blob_crc(blob) != blob.crc32) {
        return make_result(MulticastStoreStatus::kInvalidCrc);
    }
    if (blob.counter_valid > 1U || blob.reserved != 0U ||
        (blob.counter_valid == 0U && blob.last_fcnt_down != 0U)) {
        return make_result(MulticastStoreStatus::kInvalidValue);
    }
    if (blob.group_id != config.group_id) {
        return make_result(MulticastStoreStatus::kGroupMismatch);
    }
    if (blob.config_generation != config.config_generation ||
        blob.mc_addr != config.mc_addr ||
        blob.frequency_hz != config.frequency_hz ||
        blob.datarate != config.datarate) {
        return make_result(MulticastStoreStatus::kConfigMismatch);
    }
    return make_result(MulticastStoreStatus::kOk);
}

MulticastStoreResult load_multicast_blob_unlocked(
    const MulticastStoreConfig &config,
    MulticastBlob *blob)
{
    if (!config_is_valid(config) || blob == nullptr) {
        return make_result(MulticastStoreStatus::kInvalidValue,
                           ESP_ERR_INVALID_ARG);
    }

    MulticastStoreResult result = read_blob_strict_unlocked(
        kMulticastKey, blob, sizeof(*blob));
    if (!result.ok()) {
        return result;
    }
    return validate_multicast_blob(*blob, config);
}

void export_counter_state(const MulticastBlob &blob,
                          MulticastCounterState *state)
{
    if (state != nullptr) {
        state->counter_valid = blob.counter_valid != 0U;
        state->last_fcnt_down = blob.last_fcnt_down;
    }
}

MulticastStoreResult restore_unicast_buffer_unlocked(
    LoRaWANNode &node,
    const char *key,
    uint8_t *buffer,
    size_t buffer_length,
    int16_t (LoRaWANNode::*setter)(const uint8_t *))
{
    MulticastStoreResult result = read_blob_strict_unlocked(
        key, buffer, buffer_length);
    if (!result.ok()) {
        return result;
    }

    /* RadioLib performs its own signature and semantic checks in the setter. */
    const int16_t state = (node.*setter)(buffer);
    if (state != RADIOLIB_ERR_NONE) {
        return make_result(MulticastStoreStatus::kRadioLibRejected,
                           ESP_OK, state);
    }
    return make_result(MulticastStoreStatus::kOk);
}

}  // namespace

MulticastStoreResult multicast_store_init()
{
    if (!ensure_store_mutex()) {
        return make_result(MulticastStoreStatus::kNoMemory,
                           ESP_ERR_NO_MEM);
    }

    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (s_store_open) {
        return make_result(MulticastStoreStatus::kOk);
    }

    /* Never erase NVS here: recovery requires an explicit user decision. */
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) {
        return make_result(MulticastStoreStatus::kNvsError, error);
    }

    nvs_handle_t opened_handle = 0;
    error = nvs_open(kNvsNamespace, NVS_READWRITE, &opened_handle);
    if (error != ESP_OK) {
        return make_result(MulticastStoreStatus::kNvsError, error);
    }

    s_store_handle = opened_handle;
    s_store_open = true;
    return make_result(MulticastStoreStatus::kOk);
}

void multicast_store_close()
{
    if (s_store_mutex == nullptr) {
        return;
    }
    StoreGuard guard;
    if (!guard.locked()) {
        return;
    }
    if (s_store_open) {
        nvs_close(s_store_handle);
        s_store_handle = 0;
        s_store_open = false;
    }
}

UnicastRestoreReport multicast_store_restore_unicast(LoRaWANNode &node)
{
    UnicastRestoreReport report = {
        not_initialized_result(),
        not_initialized_result(),
    };

    StoreGuard guard;
    if (!guard.locked()) {
        report.nonces = lock_error_result();
        report.session = lock_error_result();
        return report;
    }
    if (!s_store_open) {
        return report;
    }

    uint8_t nonce_buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE] = {};
    report.nonces = restore_unicast_buffer_unlocked(
        node, kNonceKey, nonce_buffer, sizeof(nonce_buffer),
        &LoRaWANNode::setBufferNonces);

    if (!report.nonces.ok()) {
        report.session = make_result(
            MulticastStoreStatus::kPrerequisiteFailed,
            ESP_ERR_INVALID_STATE);
        return report;
    }

    uint8_t session_buffer[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};
    report.session = restore_unicast_buffer_unlocked(
        node, kSessionKey, session_buffer, sizeof(session_buffer),
        &LoRaWANNode::setBufferSession);
    return report;
}

MulticastStoreResult multicast_store_save_unicast_nonces(LoRaWANNode &node)
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }

    const uint8_t *nonces = node.getBufferNonces();
    if (nonces == nullptr) {
        return make_result(MulticastStoreStatus::kInvalidValue,
                           ESP_ERR_INVALID_ARG);
    }
    const esp_err_t error = nvs_set_blob(
        s_store_handle, kNonceKey, nonces,
        RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (error != ESP_OK) {
        return close_after_write_error_unlocked(error);
    }
    return commit_unlocked();
}

MulticastStoreResult multicast_store_save_unicast_state(LoRaWANNode &node)
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }

    const uint8_t *nonces = node.getBufferNonces();
    const uint8_t *session = node.getBufferSession();
    if (nonces == nullptr || session == nullptr) {
        return make_result(MulticastStoreStatus::kInvalidValue,
                           ESP_ERR_INVALID_ARG);
    }

    /* Keep nonce-first ordering so a partial write cannot revive an old nonce. */
    esp_err_t error = nvs_set_blob(
        s_store_handle, kNonceKey, nonces,
        RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    if (error != ESP_OK) {
        return close_after_write_error_unlocked(error);
    }
    error = nvs_set_blob(s_store_handle, kSessionKey, session,
                         RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
    if (error != ESP_OK) {
        return close_after_write_error_unlocked(error);
    }
    return commit_unlocked();
}

MulticastStoreResult multicast_store_prepare_multicast(
    const MulticastStoreConfig &config,
    MulticastCounterState *state)
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }
    if (!config_is_valid(config)) {
        return make_result(MulticastStoreStatus::kInvalidValue,
                           ESP_ERR_INVALID_ARG);
    }

    MulticastBlob blob = {};
    MulticastStoreResult result = load_multicast_blob_unlocked(config, &blob);
    if (result.status == MulticastStoreStatus::kNotFound) {
        blob = make_multicast_blob(config, false, 0U);
        result = write_multicast_blob_unlocked(blob);
        if (!result.ok()) {
            return result;
        }
        export_counter_state(blob, state);
        return make_result(MulticastStoreStatus::kCreated);
    }
    if (!result.ok()) {
        return result;
    }

    export_counter_state(blob, state);
    return make_result(MulticastStoreStatus::kOk);
}

MulticastStoreResult multicast_store_load_multicast(
    const MulticastStoreConfig &config,
    MulticastCounterState *state)
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }

    MulticastBlob blob = {};
    MulticastStoreResult result = load_multicast_blob_unlocked(config, &blob);
    if (!result.ok()) {
        return result;
    }
    export_counter_state(blob, state);
    return make_result(MulticastStoreStatus::kOk);
}

MulticastStoreResult multicast_store_accept_multicast_fcnt(
    const MulticastStoreConfig &config,
    uint32_t event_fcnt,
    MulticastCounterState *state)
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }

    MulticastBlob blob = {};
    MulticastStoreResult result = load_multicast_blob_unlocked(config, &blob);
    if (!result.ok()) {
        return result;
    }
    /* Persist-before-deliver requires strictly increasing event counters. */
    if (blob.counter_valid != 0U && event_fcnt <= blob.last_fcnt_down) {
        export_counter_state(blob, state);
        return make_result(MulticastStoreStatus::kReplayRejected);
    }

    const MulticastBlob updated = make_multicast_blob(config, true,
                                                       event_fcnt);
    result = write_multicast_blob_unlocked(updated);
    if (!result.ok()) {
        return result;
    }
    export_counter_state(updated, state);
    return make_result(MulticastStoreStatus::kOk);
}

MulticastStoreResult multicast_store_clear_unicast_state()
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }

    const char *const keys[] = {kNonceKey, kSessionKey};
    bool erased = false;
    for (const char *key : keys) {
        const esp_err_t error = nvs_erase_key(s_store_handle, key);
        if (error == ESP_OK) {
            erased = true;
        } else if (error != ESP_ERR_NVS_NOT_FOUND) {
            return close_after_write_error_unlocked(error);
        }
    }
    return erased ? commit_unlocked()
                  : make_result(MulticastStoreStatus::kNotFound,
                                ESP_ERR_NVS_NOT_FOUND);
}

MulticastStoreResult multicast_store_clear_multicast()
{
    StoreGuard guard;
    if (!guard.locked()) {
        return lock_error_result();
    }
    if (!s_store_open) {
        return not_initialized_result();
    }

    const esp_err_t error = nvs_erase_key(s_store_handle, kMulticastKey);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return make_result(MulticastStoreStatus::kNotFound, error);
    }
    if (error != ESP_OK) {
        return close_after_write_error_unlocked(error);
    }
    return commit_unlocked();
}

const char *multicast_store_status_name(MulticastStoreStatus status)
{
    switch (status) {
    case MulticastStoreStatus::kOk:
        return "OK";
    case MulticastStoreStatus::kCreated:
        return "CREATED";
    case MulticastStoreStatus::kNotInitialized:
        return "NOT_INITIALIZED";
    case MulticastStoreStatus::kNotFound:
        return "NOT_FOUND";
    case MulticastStoreStatus::kInvalidLength:
        return "INVALID_LENGTH";
    case MulticastStoreStatus::kInvalidMagic:
        return "INVALID_MAGIC";
    case MulticastStoreStatus::kUnsupportedSchema:
        return "UNSUPPORTED_SCHEMA";
    case MulticastStoreStatus::kInvalidCrc:
        return "INVALID_CRC";
    case MulticastStoreStatus::kInvalidValue:
        return "INVALID_VALUE";
    case MulticastStoreStatus::kGroupMismatch:
        return "GROUP_MISMATCH";
    case MulticastStoreStatus::kConfigMismatch:
        return "CONFIG_MISMATCH";
    case MulticastStoreStatus::kPrerequisiteFailed:
        return "PREREQUISITE_FAILED";
    case MulticastStoreStatus::kReplayRejected:
        return "REPLAY_REJECTED";
    case MulticastStoreStatus::kRadioLibRejected:
        return "RADIOLIB_REJECTED";
    case MulticastStoreStatus::kNvsError:
        return "NVS_ERROR";
    case MulticastStoreStatus::kNoMemory:
        return "NO_MEMORY";
    case MulticastStoreStatus::kLockError:
        return "LOCK_ERROR";
    default:
        return "UNKNOWN";
    }
}

