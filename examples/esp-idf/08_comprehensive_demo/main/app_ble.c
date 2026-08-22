#include "app_tests.h"

#include <inttypes.h>
#include <stdatomic.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ble_scan";

static atomic_bool s_started;
static atomic_bool s_stop_requested;
static atomic_uint_least32_t s_scan_count;
static bool s_host_stopped;

static int gap_event(struct ble_gap_event *event, void *arg);

static void start_passive_scan(void)
{
    if (atomic_load_explicit(&s_stop_requested, memory_order_acquire)) {
        return;
    }

    uint8_t own_addr_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer BLE address type failed: rc=%d", rc);
        return;
    }

    const struct ble_gap_disc_params params = {
        .itvl = 0,
        .window = 0,
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 1,
    };
    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params,
                      gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "start passive BLE scan failed: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "continuous passive BLE scan started");
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const uint32_t count = atomic_fetch_add_explicit(
                                   &s_scan_count, 1, memory_order_relaxed) +
                               1;
        if (count <= 5 || (count % 50U) == 0U) {
            const uint8_t *addr = event->disc.addr.val;
            ESP_LOGI(TAG,
                     "scan=%" PRIu32 " addr=%02X:%02X:%02X:%02X:%02X:%02X RSSI=%d",
                     count, addr[5], addr[4], addr[3], addr[2], addr[1],
                     addr[0], event->disc.rssi);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (!atomic_load_explicit(&s_stop_requested, memory_order_acquire)) {
            ESP_LOGW(TAG, "BLE scan ended reason=%d; restarting",
                     event->disc_complete.reason);
            start_passive_scan();
        }
        return 0;
    default:
        return 0;
    }
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset reason=%d", reason);
}

static void ble_on_sync(void)
{
    start_passive_scan();
}

static void ble_host_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t app_ble_start(void)
{
    if (atomic_load_explicit(&s_started, memory_order_acquire)) {
        if (!s_host_stopped) {
            return ESP_OK;
        }
        const esp_err_t cleanup_ret = app_ble_stop();
        if (cleanup_ret != ESP_OK) {
            return cleanup_ret;
        }
    }
    atomic_store_explicit(&s_stop_requested, false, memory_order_release);
    atomic_store_explicit(&s_scan_count, 0, memory_order_relaxed);
    s_host_stopped = false;

    const esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    atomic_store_explicit(&s_started, true, memory_order_release);
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

esp_err_t app_ble_stop(void)
{
    if (!atomic_load_explicit(&s_started, memory_order_acquire)) {
        return ESP_OK;
    }

    atomic_store_explicit(&s_stop_requested, true, memory_order_release);
    if (!s_host_stopped) {
        (void)ble_gap_disc_cancel();
        const int stop_rc = nimble_port_stop();
        if (stop_rc != 0) {
            ESP_LOGE(TAG, "NimBLE stop failed: rc=%d", stop_rc);
            return ESP_FAIL;
        }
        s_host_stopped = true;
    }

    const int rc = nimble_port_deinit();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE deinit failed: rc=%d", rc);
        return ESP_FAIL;
    }
    s_host_stopped = false;
    atomic_store_explicit(&s_started, false, memory_order_release);
    ESP_LOGI(TAG, "NimBLE controller and host released");
    return ESP_OK;
}

bool app_ble_is_started(void)
{
    return atomic_load_explicit(&s_started, memory_order_acquire);
}

uint32_t app_ble_scan_count(void)
{
    return atomic_load_explicit(&s_scan_count, memory_order_relaxed);
}
