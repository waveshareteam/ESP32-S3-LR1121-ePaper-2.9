#include "app_tests.h"

#include <stdio.h>

#include "app_config.h"
#include "board_radio.h"

esp_err_t test_radio_run(app_test_result_t *result)
{
    board_radio_config_t cfg = TEST_LORA_CONFIG_INIT();
    board_radio_status_t status = {0};
    esp_err_t ret = board_radio_init(&cfg, &status);
    if (ret != ESP_OK) {
        char line[48];
        snprintf(line, sizeof(line), "state %d %s", status.state,
                 board_radio_state_name(status.state));
        app_result_set(result, "RADIO", APP_TEST_FAIL,
                       "LR1121 init fail", line);
        return ret;
    }

    char line1[48];
    char line2[48];
    snprintf(line1, sizeof(line1), "RX %.1fMHz SF%u",
             (double)cfg.frequency_mhz, cfg.spreading_factor);
    snprintf(line2, sizeof(line2), "BW%.1fk CR4/%u PWR%d",
             (double)cfg.bandwidth_khz, cfg.coding_rate,
             (int)cfg.output_power_dbm);
    app_result_set(result, "RADIO", APP_TEST_PASS, line1, line2);
    return ESP_OK;
}

