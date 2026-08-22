#include "abp_state.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "esp_rom_crc.h"
#include "nvs_flash.h"

namespace abp_state {
namespace {

constexpr char kNamespace[] = "lorawan_abp";
constexpr char kSlotAKey[] = "state_a";
constexpr char kSlotBKey[] = "state_b";
constexpr uint32_t kMagic = 0x31504241U;  // "ABP1" in little-endian storage.
constexpr uint16_t kFormatVersion = 1;
constexpr uint32_t kKnownFlags = kFlagRadioTransactionPending;

struct PersistedRecord {
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_length;
    uint16_t record_length;
    uint16_t header_reserved;
    uint32_t radiolib_version;
    uint32_t dev_addr;
    uint32_t config_fingerprint;
    uint32_t app_sequence;
    uint32_t generation;
    uint32_t flags;
    uint8_t mac_version;
    uint8_t reserved[3];
    uint8_t nonces[kNoncesSize];
    uint8_t session[kSessionSize];
    uint32_t crc32;
};

constexpr std::size_t kHeaderLength = offsetof(PersistedRecord, nonces);
constexpr std::size_t kCrcLength = offsetof(PersistedRecord, crc32);

static_assert(std::is_standard_layout<PersistedRecord>::value,
              "NVS record must have a stable standard layout");
static_assert(kHeaderLength == 40, "Unexpected ABP state header layout");
static_assert(kCrcLength == 356, "Unexpected ABP state CRC offset");
static_assert(sizeof(PersistedRecord) == 360,
              "Unexpected ABP state record layout");
static_assert(sizeof(kNamespace) - 1 <= 15, "NVS namespace is too long");
static_assert(sizeof(kSlotAKey) - 1 <= 15, "NVS key is too long");
static_assert(sizeof(kSlotBKey) - 1 <= 15, "NVS key is too long");

enum class SlotReadStatus : uint8_t {
    Missing,
    Valid,
    Invalid,
    StorageError,
};

struct SlotReadResult {
    SlotReadStatus status;
    esp_err_t error;
};

void secure_zero(void *buffer, std::size_t length) {
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(buffer);
    while (length-- > 0) {
        *bytes++ = 0;
    }
}

uint32_t record_crc(const PersistedRecord &record) {
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&record),
                            static_cast<uint32_t>(kCrcLength));
}

bool reserved_fields_are_zero(const PersistedRecord &record) {
    return record.header_reserved == 0 && record.reserved[0] == 0 &&
           record.reserved[1] == 0 && record.reserved[2] == 0;
}

bool record_matches(const PersistedRecord &record, const Identity &identity) {
    return record.magic == kMagic &&
           record.format_version == kFormatVersion &&
           record.header_length == kHeaderLength &&
           record.record_length == sizeof(PersistedRecord) &&
           record.radiolib_version == RADIOLIB_VERSION &&
           record.dev_addr == identity.dev_addr &&
           record.mac_version == identity.mac_version &&
           record.config_fingerprint == identity.config_fingerprint &&
           record.generation != 0 && (record.flags & ~kKnownFlags) == 0 &&
           reserved_fields_are_zero(record) && record.crc32 == record_crc(record);
}

SlotReadResult read_slot(nvs_handle_t handle, const char *key,
                         const Identity &identity, PersistedRecord *record) {
    std::size_t length = 0;
    esp_err_t error = nvs_get_blob(handle, key, nullptr, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return {SlotReadStatus::Missing, ESP_OK};
    }
    if (error != ESP_OK) {
        return {SlotReadStatus::StorageError, error};
    }
    if (length != sizeof(*record)) {
        return {SlotReadStatus::Invalid, ESP_OK};
    }

    length = sizeof(*record);
    error = nvs_get_blob(handle, key, record, &length);
    if (error == ESP_ERR_NVS_INVALID_LENGTH || length != sizeof(*record)) {
        return {SlotReadStatus::Invalid, ESP_OK};
    }
    if (error != ESP_OK) {
        return {SlotReadStatus::StorageError, error};
    }
    if (!record_matches(*record, identity)) {
        return {SlotReadStatus::Invalid, ESP_OK};
    }
    return {SlotReadStatus::Valid, ESP_OK};
}

void copy_to_snapshot(const PersistedRecord &record, Snapshot *snapshot) {
    std::memcpy(snapshot->nonces, record.nonces, sizeof(snapshot->nonces));
    std::memcpy(snapshot->session, record.session, sizeof(snapshot->session));
    snapshot->app_sequence = record.app_sequence;
    snapshot->generation = record.generation;
    snapshot->flags = record.flags;
}

}  // namespace

Store::~Store() {
    end();
}

void Store::reset_load_tracking() {
    load_attempted_ = false;
    last_generation_ = 0;
    last_slot_ = Slot::None;
}

esp_err_t Store::begin() {
    if (is_open_) {
        return ESP_OK;
    }

    const esp_err_t init_error = nvs_flash_init();
    if (init_error != ESP_OK) {
        // In particular, do not erase the partition for NO_FREE_PAGES or
        // NEW_VERSION_FOUND: losing ABP counters can make the server reject
        // frames and can enable counter reuse.
        return init_error;
    }

    const esp_err_t open_error = nvs_open(kNamespace, NVS_READWRITE, &handle_);
    if (open_error != ESP_OK) {
        handle_ = 0;
        return open_error;
    }

    is_open_ = true;
    reset_load_tracking();
    return ESP_OK;
}

void Store::end() {
    if (is_open_) {
        nvs_close(handle_);
    }
    handle_ = 0;
    is_open_ = false;
    reset_load_tracking();
}

LoadResult Store::load(const Identity &identity, Snapshot *snapshot) {
    if (!is_open_ || snapshot == nullptr || identity.dev_addr == 0) {
        return {LoadStatus::StorageError, ESP_ERR_INVALID_ARG};
    }

    reset_load_tracking();
    std::memset(snapshot, 0, sizeof(*snapshot));
    PersistedRecord record_a = {};
    PersistedRecord record_b = {};
    const SlotReadResult result_a =
        read_slot(handle_, kSlotAKey, identity, &record_a);
    const SlotReadResult result_b =
        read_slot(handle_, kSlotBKey, identity, &record_b);

    if (result_a.status == SlotReadStatus::StorageError ||
        result_b.status == SlotReadStatus::StorageError) {
        const esp_err_t error = result_a.status == SlotReadStatus::StorageError
                                    ? result_a.error
                                    : result_b.error;
        secure_zero(&record_a, sizeof(record_a));
        secure_zero(&record_b, sizeof(record_b));
        return {LoadStatus::StorageError, error};
    }

    const bool valid_a = result_a.status == SlotReadStatus::Valid;
    const bool valid_b = result_b.status == SlotReadStatus::Valid;
    const bool invalid_a = result_a.status == SlotReadStatus::Invalid;
    const bool invalid_b = result_b.status == SlotReadStatus::Invalid;
    const bool missing_a = result_a.status == SlotReadStatus::Missing;
    const bool missing_b = result_b.status == SlotReadStatus::Missing;

    if (invalid_a || invalid_b) {
        secure_zero(&record_a, sizeof(record_a));
        secure_zero(&record_b, sizeof(record_b));
        return {LoadStatus::Invalid, ESP_OK};
    }

    if (!valid_a && !valid_b) {
        secure_zero(&record_a, sizeof(record_a));
        secure_zero(&record_b, sizeof(record_b));
        if (!missing_a || !missing_b) {
            return {LoadStatus::Invalid, ESP_OK};
        }
        load_attempted_ = true;
        return {LoadStatus::NotFound, ESP_OK};
    }

    const PersistedRecord *selected = nullptr;
    if (valid_a && valid_b) {
        const uint32_t newer_generation =
            record_a.generation > record_b.generation
                ? record_a.generation
                : record_b.generation;
        const uint32_t older_generation =
            record_a.generation > record_b.generation
                ? record_b.generation
                : record_a.generation;
        const bool generations_are_consistent =
            newer_generation - older_generation == 1U &&
            (record_a.generation & 1U) == 1U &&
            (record_b.generation & 1U) == 0U;
        if (!generations_are_consistent) {
            secure_zero(&record_a, sizeof(record_a));
            secure_zero(&record_b, sizeof(record_b));
            return {LoadStatus::Invalid, ESP_OK};
        }
        if (record_b.generation > record_a.generation) {
            selected = &record_b;
            last_slot_ = Slot::B;
        } else {
            selected = &record_a;
            last_slot_ = Slot::A;
        }
    } else if (valid_a && missing_b && record_a.generation == 1U) {
        // The first committed snapshot is written to slot A. Slot B is only
        // allowed to be absent until the second successful commit.
        selected = &record_a;
        last_slot_ = Slot::A;
    } else {
        secure_zero(&record_a, sizeof(record_a));
        secure_zero(&record_b, sizeof(record_b));
        return {LoadStatus::Invalid, ESP_OK};
    }

    copy_to_snapshot(*selected, snapshot);
    load_attempted_ = true;
    last_generation_ = selected->generation;
    secure_zero(&record_a, sizeof(record_a));
    secure_zero(&record_b, sizeof(record_b));
    return {LoadStatus::Restored, ESP_OK};
}

esp_err_t Store::save_reserved(const Identity &identity, Snapshot *snapshot) {
    return save(identity, snapshot, kFlagRadioTransactionPending);
}

esp_err_t Store::save_actual(const Identity &identity, Snapshot *snapshot) {
    return save(identity, snapshot, 0);
}

esp_err_t Store::save(const Identity &identity, Snapshot *snapshot,
                      uint32_t flags) {
    if (!is_open_ || !load_attempted_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (snapshot == nullptr || identity.dev_addr == 0 ||
        (flags & ~kKnownFlags) != 0 || last_generation_ == UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    PersistedRecord record = {};
    record.magic = kMagic;
    record.format_version = kFormatVersion;
    record.header_length = static_cast<uint16_t>(kHeaderLength);
    record.record_length = static_cast<uint16_t>(sizeof(record));
    record.radiolib_version = RADIOLIB_VERSION;
    record.dev_addr = identity.dev_addr;
    record.config_fingerprint = identity.config_fingerprint;
    record.app_sequence = snapshot->app_sequence;
    record.generation = last_generation_ + 1;
    record.flags = flags;
    record.mac_version = identity.mac_version;
    std::memcpy(record.nonces, snapshot->nonces, sizeof(record.nonces));
    std::memcpy(record.session, snapshot->session, sizeof(record.session));
    record.crc32 = record_crc(record);

    const Slot next_slot = last_slot_ == Slot::A ? Slot::B : Slot::A;
    const char *key = next_slot == Slot::A ? kSlotAKey : kSlotBKey;
    esp_err_t error = nvs_set_blob(handle_, key, &record, sizeof(record));
    if (error == ESP_OK) {
        error = nvs_commit(handle_);
    }
    secure_zero(&record, sizeof(record));
    if (error != ESP_OK) {
        return error;
    }

    last_generation_ += 1;
    last_slot_ = next_slot;
    snapshot->generation = last_generation_;
    snapshot->flags = flags;
    return ESP_OK;
}

esp_err_t Store::clear() {
    if (!is_open_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = nvs_erase_all(handle_);
    if (error == ESP_OK) {
        error = nvs_commit(handle_);
    }
    if (error != ESP_OK) {
        return error;
    }

    load_attempted_ = true;
    last_generation_ = 0;
    last_slot_ = Slot::None;
    return ESP_OK;
}

const char *load_status_name(LoadStatus status) {
    switch (status) {
        case LoadStatus::Restored:
            return "RESTORED";
        case LoadStatus::NotFound:
            return "NOT_FOUND";
        case LoadStatus::Invalid:
            return "INVALID";
        case LoadStatus::StorageError:
            return "STORAGE_ERROR";
    }
    return "UNKNOWN";
}

}  // namespace abp_state
