#include "app_tests.h"

#include <stdio.h>

#include "board_epaper.h"
#include "board_support.h"

esp_err_t test_epaper_run(app_test_result_t *result)
{
    board_epaper_t panel = {0};
    esp_err_t ret = board_epaper_start(&panel);
    if (ret != ESP_OK) {
        app_result_set(result, "EPD", APP_TEST_FAIL,
                       "EPD init fail", esp_err_to_name(ret));
        return ret;
    }
    board_epaper_hw_config_t hw_config = {0};
    ret = board_epaper_get_hw_config(&hw_config);
    if (ret != ESP_OK) {
        app_result_set(result, "EPD", APP_TEST_FAIL,
                       "EPD config fail", esp_err_to_name(ret));
        return ret;
    }

    char l1[48];
    char l2[48];
    snprintf(l1, sizeof(l1), "EPD %ux%u OK", panel.info.width, panel.info.height);
    snprintf(l2, sizeof(l2), "CS%d DC%d BUSY%d", (int)hw_config.cs,
             (int)hw_config.dc, (int)hw_config.busy);
    app_result_set(result, "EPD", APP_TEST_PASS, l1, l2);
    return ESP_OK;
}
