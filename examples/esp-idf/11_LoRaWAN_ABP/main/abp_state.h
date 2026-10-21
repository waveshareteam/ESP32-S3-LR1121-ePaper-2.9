#pragma once

#include <cstddef>
#include <cstdint>

#include <RadioLib.h>
#include "esp_err.h"
#include "nvs.h"

namespace abp_state {

constexpr std::size_t kNoncesSize = RADIOLIB_LORAWAN_NONCES_BUF_SIZE;
constexpr std::size_t kSessionSize = RADIOLIB_LORAWAN_SESSION_BUF_SIZE;

static_assert(kNoncesSize == 14,
              "RadioLib nonces buffer layout changed; review ABP persistence");
static_assert(kSessionSize == 302,
              "RadioLib session buffer layout changed; review ABP persistence");

// Set before a Class A radio transaction and cleared only after the resulting
// session is committed. A restored pending transaction has an unknown
// downlink outcome and must fail closed instead of reusing stale counters.
constexpr uint32_t kFlagRadioTransactionPending = 1U << 0;

struct Identity {
    uint32_t dev_addr = 0;
    uint8_t mac_version = 0;
    uint32_t config_fingerprint = 0;
};

struct Snapshot {
    uint8_t nonces[kNoncesSize] = {};
    uint8_t session[kSessionSize] = {};
    uint32_t app_sequence = 0;
    uint32_t generation = 0;
    uint32_t flags = 0;
};

enum class LoadStatus : uint8_t {
    Restored,
    NotFound,
    Invalid,
    StorageError,
};

struct LoadResult {
    LoadStatus status = LoadStatus::StorageError;
    esp_err_t error = ESP_FAIL;
};

class Store {
  public:
    Store() = default;
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Initializes the default NVS partition and opens this module's namespace.
    // Deliberately does not erase NVS on initialization errors.
    esp_err_t begin();
    void end();

    // load() must be called before the first save. A corrupt or incompatible
    // record is reported as Invalid and must not be treated as a new session.
    LoadResult load(const Identity &identity, Snapshot *snapshot);

    // On success these functions assign the committed generation and flags to
    // snapshot. save_reserved() is used before radio transmission;
    // save_actual() is used immediately after sendReceive() returns.
    esp_err_t save_reserved(const Identity &identity, Snapshot *snapshot);
    esp_err_t save_actual(const Identity &identity, Snapshot *snapshot);

    // Erases only the lorawan_abp namespace. The caller must guard this with an
    // explicit maintenance action coordinated with the network server.
    esp_err_t clear();

    bool is_open() const { return is_open_; }
    uint32_t last_generation() const { return last_generation_; }

  private:
    enum class Slot : uint8_t {
        None,
        A,
        B,
    };

    esp_err_t save(const Identity &identity, Snapshot *snapshot, uint32_t flags);
    void reset_load_tracking();

    nvs_handle_t handle_ = 0;
    bool is_open_ = false;
    bool load_attempted_ = false;
    uint32_t last_generation_ = 0;
    Slot last_slot_ = Slot::None;
};

const char *load_status_name(LoadStatus status);

}  // namespace abp_state
