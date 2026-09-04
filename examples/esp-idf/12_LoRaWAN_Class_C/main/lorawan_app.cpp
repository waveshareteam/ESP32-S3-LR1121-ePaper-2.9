/**
 * @file lorawan_app.cpp
 * @brief Starts the user interface and the single-owner radio task.
 */

#include "lorawan_app.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lorawan_config.h"
#include "lorawan_node.h"
#include "lorawan_runtime.h"
#include "lorawan_ui.h"

namespace lorawan {
namespace {

constexpr char kTag[] = "lorawan_class_c";

/** Publishes the initial state and waits for one bounded display attempt. */
void start_initial_display(AppContext &startup)
{
    const esp_err_t ui_start = lorawan_ui_start();
    if (ui_start != ESP_OK) {
        ESP_LOGE(kTag, "start e-paper UI task failed: %s",
                 esp_err_to_name(ui_start));
        return;
    }

    const esp_err_t post_state = lorawan_ui_post(startup.ui, true);
    if (post_state != ESP_OK) {
        ESP_LOGW(kTag, "post initial UI state failed: %s",
                 esp_err_to_name(post_state));
    }

    const esp_err_t first_display = lorawan_ui_wait_first_attempt(
        LORAWAN_DISPLAY_FIRST_TIMEOUT_MS);
    if (first_display != ESP_OK) {
        ESP_LOGW(kTag, "first e-paper attempt did not succeed: %s",
                 esp_err_to_name(first_display));
    }
}

}  // namespace

void start_application()
{
    AppContext startup = {};
    copy_text(startup.ui.region, sizeof(startup.ui.region),
              LORAWAN_REGION_NAME);
    startup.ui.join_eui = LORAWAN_JOIN_EUI;

    const bool configured = credentials_configured();
    copy_text(startup.ui.status, sizeof(startup.ui.status),
              configured ? "RADIO INIT" : "CONFIG REQUIRED");
    copy_text(startup.ui.detail, sizeof(startup.ui.detail),
              configured ? "WAIT FIRST DISPLAY"
                         : "SET OTAA CREDENTIALS");
    start_initial_display(startup);

    // Invalid placeholders never reach RadioLib or generate OTAA traffic.
    if (!configured) {
        ESP_LOGE(kTag,
                 "OTAA credentials are placeholders; radio activation is "
                 "disabled and no credential value is logged");
        return;
    }

    // This is the only task allowed to own LR1121 and LoRaWANNode objects.
    const BaseType_t created = xTaskCreate(
        radio_task, "lorawan_class_c_radio",
        LORAWAN_RADIO_TASK_STACK, nullptr,
        LORAWAN_RADIO_TASK_PRIORITY, nullptr);
    if (created == pdPASS) {
        return;
    }

    ESP_LOGE(kTag, "create single-owner radio task failed");
    copy_text(startup.ui.status, sizeof(startup.ui.status), "ERROR");
    copy_text(startup.ui.detail, sizeof(startup.ui.detail),
              "RADIO TASK CREATE");
    startup.ui.last_code = ESP_ERR_NO_MEM;
    (void)lorawan_ui_post(startup.ui, true);
}

}  // namespace lorawan
