#include "app_tests.h"

#include <stdatomic.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "wifi_test";

static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static bool s_wifi_handler_registered;
static bool s_ip_handler_registered;
static bool s_wifi_initialized;
static bool s_netif_owned;
static bool s_event_loop_owned;
static atomic_bool s_started;
static atomic_bool s_stopping;
static atomic_bool s_sta_configured;
static bool s_sta_connected;
static uint8_t s_ap_clients;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static bool configured_string(const char *value)
{
    return value && value[0] != '\0' &&
           strcmp(value, "YOUR_SSID") != 0 &&
           strcmp(value, "YOUR_PASSWORD") != 0;
}

static void set_sta_connected(bool connected)
{
    taskENTER_CRITICAL(&s_state_lock);
    s_sta_connected = connected;
    taskEXIT_CRITICAL(&s_state_lock);
}

static uint8_t update_ap_clients(int delta)
{
    taskENTER_CRITICAL(&s_state_lock);
    if (delta > 0 && s_ap_clients < UINT8_MAX) {
        ++s_ap_clients;
    } else if (delta < 0 && s_ap_clients > 0) {
        --s_ap_clients;
    }
    const uint8_t count = s_ap_clients;
    taskEXIT_CRITICAL(&s_state_lock);
    return count;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (atomic_load_explicit(&s_sta_configured, memory_order_acquire) &&
            !atomic_load_explicit(&s_stopping, memory_order_acquire)) {
            const esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "STA initial connect failed: %s",
                         esp_err_to_name(ret));
            }
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        set_sta_connected(false);
        ESP_LOGW(TAG, "STA disconnected reason=%u%s",
                 event ? (unsigned)event->reason : 0,
                 atomic_load_explicit(&s_stopping, memory_order_acquire)
                     ? "; stopping"
                     : "; reconnecting");
        if (atomic_load_explicit(&s_sta_configured, memory_order_acquire) &&
            !atomic_load_explicit(&s_stopping, memory_order_acquire)) {
            const esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "STA reconnect request failed: %s",
                         esp_err_to_name(ret));
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        wifi_ap_record_t ap = {0};
        const esp_err_t ap_ret = esp_wifi_sta_get_ap_info(&ap);
        set_sta_connected(true);
        if (event) {
            ESP_LOGI(TAG, "STA IP=" IPSTR " gateway=" IPSTR " RSSI=%s%d dBm",
                     IP2STR(&event->ip_info.ip),
                     IP2STR(&event->ip_info.gw),
                     ap_ret == ESP_OK ? "" : "N/A ",
                     ap_ret == ESP_OK ? (int)ap.rssi : 0);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = data;
        const uint8_t clients = update_ap_clients(1);
        if (event) {
            ESP_LOGI(TAG, "SoftAP client " MACSTR " connected AID=%d clients=%u",
                     MAC2STR(event->mac), event->aid, (unsigned)clients);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = data;
        const uint8_t clients = update_ap_clients(-1);
        if (event) {
            ESP_LOGI(TAG,
                     "SoftAP client " MACSTR " disconnected AID=%d clients=%u",
                     MAC2STR(event->mac), event->aid, (unsigned)clients);
        }
    }
}

static void record_first_error(esp_err_t ret, const char *operation,
                               esp_err_t *first_error)
{
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(ret));
        if (*first_error == ESP_OK) {
            *first_error = ret;
        }
    }
}

static esp_err_t wifi_cleanup(void)
{
    esp_err_t first_error = ESP_OK;
    atomic_store_explicit(&s_stopping, true, memory_order_release);
    set_sta_connected(false);
    taskENTER_CRITICAL(&s_state_lock);
    s_ap_clients = 0;
    taskEXIT_CRITICAL(&s_state_lock);

    if (atomic_load_explicit(&s_started, memory_order_acquire)) {
        esp_err_t ret = esp_wifi_stop();
        if (ret == ESP_ERR_WIFI_NOT_STARTED) {
            ret = ESP_OK;
        }
        record_first_error(ret, "stop Wi-Fi", &first_error);
        if (ret != ESP_OK) {
            goto done;
        }
        atomic_store_explicit(&s_started, false, memory_order_release);
    }

    if (s_ip_handler_registered) {
        const esp_err_t ret = esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
        record_first_error(ret, "unregister IP handler", &first_error);
        if (ret == ESP_OK) {
            s_ip_handler_registered = false;
        }
    }
    if (s_wifi_handler_registered) {
        const esp_err_t ret = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler);
        record_first_error(ret, "unregister Wi-Fi handler", &first_error);
        if (ret == ESP_OK) {
            s_wifi_handler_registered = false;
        }
    }
    if (s_ip_handler_registered || s_wifi_handler_registered) {
        goto done;
    }
    if (s_wifi_initialized) {
        const esp_err_t ret = esp_wifi_deinit();
        record_first_error(ret, "deinitialize Wi-Fi", &first_error);
        if (ret != ESP_OK) {
            goto done;
        }
        s_wifi_initialized = false;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_event_loop_owned) {
        const esp_err_t ret = esp_event_loop_delete_default();
        record_first_error(ret, "delete default event loop", &first_error);
        if (ret != ESP_OK) {
            goto done;
        }
        s_event_loop_owned = false;
    }
    if (s_netif_owned) {
#if CONFIG_ESP_NETIF_TCPIP_LWIP
        ESP_LOGI(TAG,
                 "global lwIP core remains initialized: ESP-IDF does not "
                 "support software esp-netif deinitialization");
#else
        record_first_error(esp_netif_deinit(), "deinitialize esp-netif",
                           &first_error);
#endif
        s_netif_owned = false;
    }

done:
    if (first_error == ESP_OK) {
        atomic_store_explicit(&s_sta_configured, false, memory_order_release);
    }
    atomic_store_explicit(&s_stopping, false, memory_order_release);
    return first_error;
}

esp_err_t app_wifi_start(void)
{
    if (atomic_load_explicit(&s_started, memory_order_acquire)) {
        return ESP_OK;
    }
    if (s_wifi_initialized || s_ap_netif || s_sta_netif ||
        s_wifi_handler_registered || s_ip_handler_registered ||
        s_event_loop_owned || s_netif_owned) {
        const esp_err_t cleanup_ret = wifi_cleanup();
        if (cleanup_ret != ESP_OK) {
            return cleanup_ret;
        }
    }

    esp_err_t ret = esp_netif_init();
    if (ret == ESP_OK) {
        s_netif_owned = true;
    } else if (ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret == ESP_OK) {
        s_event_loop_owned = true;
    } else if (ret != ESP_ERR_INVALID_STATE) {
        (void)wifi_cleanup();
        return ret;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_ap_netif || !s_sta_netif) {
        (void)wifi_cleanup();
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init_config);
    if (ret != ESP_OK) {
        (void)wifi_cleanup();
        return ret;
    }
    s_wifi_initialized = true;

    ret = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL,
        &s_wifi_handler);
    if (ret != ESP_OK) {
        (void)wifi_cleanup();
        return ret;
    }
    s_wifi_handler_registered = true;
    ret = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL,
        &s_ip_handler);
    if (ret != ESP_OK) {
        (void)wifi_cleanup();
        return ret;
    }
    s_ip_handler_registered = true;

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, TEST_WIFI_AP_SSID,
            sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, TEST_WIFI_AP_PASSWORD,
            sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(TEST_WIFI_AP_SSID);
    ap_config.ap.channel = TEST_WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = TEST_WIFI_AP_MAX_CONN;
    ap_config.ap.authmode = strlen(TEST_WIFI_AP_PASSWORD) >= 8
                                ? WIFI_AUTH_WPA2_PSK
                                : WIFI_AUTH_OPEN;

    const bool sta_configured =
        configured_string(TEST_WIFI_STA_SSID) &&
        configured_string(TEST_WIFI_STA_PASSWORD);
    atomic_store_explicit(&s_sta_configured, sta_configured,
                          memory_order_release);
    wifi_config_t sta_config = {0};
    if (sta_configured) {
        strlcpy((char *)sta_config.sta.ssid, TEST_WIFI_STA_SSID,
                sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, TEST_WIFI_STA_PASSWORD,
                sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    atomic_store_explicit(&s_stopping, false, memory_order_release);
    set_sta_connected(false);
    taskENTER_CRITICAL(&s_state_lock);
    s_ap_clients = 0;
    taskEXIT_CRITICAL(&s_state_lock);

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret == ESP_OK) ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret == ESP_OK) ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret == ESP_OK && sta_configured) {
        ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    }
    if (ret == ESP_OK) ret = esp_wifi_start();
    if (ret == ESP_OK) {
        atomic_store_explicit(&s_started, true, memory_order_release);
        ret = esp_wifi_set_ps(WIFI_PS_NONE);
    }
    if (ret != ESP_OK) {
        const esp_err_t cleanup_ret = wifi_cleanup();
        return ret != ESP_OK ? ret : cleanup_ret;
    }

    ESP_LOGI(TAG,
             "SoftAP started: SSID=%s channel=%d max_clients=%d power_save=OFF",
             TEST_WIFI_AP_SSID, TEST_WIFI_AP_CHANNEL, TEST_WIFI_AP_MAX_CONN);
    if (sta_configured) {
        ESP_LOGI(TAG, "STA credentials configured; disconnects auto-reconnect");
    } else {
        ESP_LOGW(TAG,
                 "STA NOT_CONFIGURED; set TEST_WIFI_STA_SSID/PASSWORD in app_config.h");
    }
    return ESP_OK;
}

esp_err_t app_wifi_stop(void)
{
    if (!atomic_load_explicit(&s_started, memory_order_acquire) &&
        !s_wifi_initialized && !s_ap_netif && !s_sta_netif &&
        !s_wifi_handler_registered && !s_ip_handler_registered &&
        !s_event_loop_owned && !s_netif_owned) {
        return ESP_OK;
    }
    return wifi_cleanup();
}

bool app_wifi_is_started(void)
{
    return atomic_load_explicit(&s_started, memory_order_acquire);
}

bool app_wifi_sta_connected(void)
{
    taskENTER_CRITICAL(&s_state_lock);
    const bool connected = s_sta_connected;
    taskEXIT_CRITICAL(&s_state_lock);
    return connected;
}

bool app_wifi_sta_configured(void)
{
    return atomic_load_explicit(&s_sta_configured, memory_order_acquire);
}

uint8_t app_wifi_ap_clients(void)
{
    taskENTER_CRITICAL(&s_state_lock);
    const uint8_t clients = s_ap_clients;
    taskEXIT_CRITICAL(&s_state_lock);
    return clients;
}
