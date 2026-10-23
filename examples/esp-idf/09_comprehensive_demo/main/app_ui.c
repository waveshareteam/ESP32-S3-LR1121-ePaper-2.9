#include "app_tests.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "board_epaper.h"

#define APP_UI_MAX_SUMMARY_LINES 7U
#define APP_UI_GRID_PRIMARY_LINES 6U

static const char *status_word(app_test_status_t status)
{
    if (status == APP_TEST_PASS) {
        return "PASS";
    }
    if (status == APP_TEST_FAIL) {
        return "FAIL";
    }
    return "N/T";
}

static const char *status_code(app_test_status_t status)
{
    if (status == APP_TEST_PASS) {
        return "P";
    }
    if (status == APP_TEST_FAIL) {
        return "F";
    }
    return "N";
}

static const char *runtime_state_label(const char *state)
{
    if (!state) {
        return "UNKNOWN";
    }
    if (strcmp(state, "ACTIVE_SELF_TEST") == 0) {
        return "SELF TEST";
    }
    if (strcmp(state, "ACTIVE_READY") == 0) {
        return "READY";
    }
    if (strcmp(state, "PREPARE_SLEEP") == 0) {
        return "SLEEP PREP";
    }
    if (strcmp(state, "WAKE_REINIT") == 0) {
        return "WAKE";
    }
    if (strcmp(state, "LOW_POWER") == 0) {
        return "LOW POWER";
    }
    if (strcmp(state, "LORA_TX") == 0) {
        return "LORA TX";
    }
    if (strcmp(state, "LORA_RX") == 0) {
        return "LORA RX";
    }
    return state;
}

void app_result_set(app_test_result_t *result, const char *title,
                    app_test_status_t status,
                    const char *line1, const char *line2)
{
    if (!result) {
        return;
    }
    result->status = status;
    snprintf(result->title, sizeof(result->title), "%s", title ? title : "TEST");
    snprintf(result->line1, sizeof(result->line1), "%s", line1 ? line1 : "");
    snprintf(result->line2, sizeof(result->line2), "%s", line2 ? line2 : "");
}

esp_err_t app_ui_init(void)
{
    return board_epaper_start(NULL);
}

esp_err_t app_ui_show_message(const char *title, const char *line1,
                              const char *line2, bool full_refresh)
{
    const char *lines[] = {
        line1 ? line1 : "",
        line2 ? line2 : "",
    };
    return board_epaper_show_lines(title, lines, 2, full_refresh);
}

esp_err_t app_ui_show_summary(const app_test_result_t *results, size_t count,
                              bool full_refresh)
{
    if (!results && count > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char lines[APP_UI_MAX_SUMMARY_LINES][48] = {0};
    const char *line_ptrs[APP_UI_MAX_SUMMARY_LINES] = {0};
    size_t pass = 0;
    size_t fail = 0;
    size_t not_tested = 0;
    for (size_t i = 0; i < count; ++i) {
        if (results[i].status == APP_TEST_PASS) {
            ++pass;
        } else if (results[i].status == APP_TEST_FAIL) {
            ++fail;
        } else {
            ++not_tested;
        }
    }

    size_t shown = count < APP_UI_GRID_PRIMARY_LINES
                       ? count
                       : APP_UI_GRID_PRIMARY_LINES;
    for (size_t i = 0; i < shown; ++i) {
        snprintf(lines[i], sizeof(lines[i]), "%-7.7s %s",
                 results[i].title[0] ? results[i].title : "TEST",
                 status_word(results[i].status));
        line_ptrs[i] = lines[i];
    }

    const size_t remaining = count - shown;
    if (remaining == 1U) {
        snprintf(lines[shown], sizeof(lines[shown]),
                 "%.10s:%s | P%u F%u N%u",
                 results[shown].title, status_code(results[shown].status),
                 (unsigned)pass, (unsigned)fail, (unsigned)not_tested);
    } else if (remaining == 2U) {
        snprintf(lines[shown], sizeof(lines[shown]),
                 "%.7s:%s %.7s:%s | P%u F%u N%u",
                 results[shown].title, status_code(results[shown].status),
                 results[shown + 1U].title,
                 status_code(results[shown + 1U].status),
                 (unsigned)pass, (unsigned)fail, (unsigned)not_tested);
    } else if (remaining > 2U) {
        snprintf(lines[shown], sizeof(lines[shown]),
                 "EXTRA:%u | P%u F%u N%u", (unsigned)remaining,
                 (unsigned)pass, (unsigned)fail, (unsigned)not_tested);
    } else {
        snprintf(lines[shown], sizeof(lines[shown]),
                 "TOTAL P%u F%u N%u", (unsigned)pass, (unsigned)fail,
                 (unsigned)not_tested);
    }
    line_ptrs[shown] = lines[shown];
    ++shown;

    return board_epaper_show_lines("BOARD TEST", line_ptrs, shown, full_refresh);
}

esp_err_t app_ui_show_runtime(const char *state, uint32_t tx_count,
                             uint32_t tx_fail_count, uint32_t rx_count,
                             uint32_t ap_clients, bool sta_connected,
                             bool ble_started, uint32_t ble_scan_count)
{
    char lines[7][48] = {0};
    const char *line_ptrs[7];
    snprintf(lines[0], sizeof(lines[0]), "MODE %s",
             runtime_state_label(state));
    snprintf(lines[1], sizeof(lines[1]), "CPU 2C 240M");
    snprintf(lines[2], sizeof(lines[2]), "AP:%lu STA:%s",
             (unsigned long)ap_clients, sta_connected ? "UP" : "WAIT");
    snprintf(lines[3], sizeof(lines[3]), "BLE:%u N:%08" PRIX32,
             ble_started ? 1U : 0U, ble_scan_count);
    snprintf(lines[4], sizeof(lines[4]), "KEY1 TX");
    snprintf(lines[5], sizeof(lines[5]), "G0 2xSLP 1xWAK");
    snprintf(lines[6], sizeof(lines[6]),
             "LORA T%08" PRIX32 " F%08" PRIX32 " R%08" PRIX32,
             tx_count, tx_fail_count, rx_count);
    for (size_t i = 0; i < 7; ++i) {
        line_ptrs[i] = lines[i];
    }
    return board_epaper_show_lines("RUNTIME", line_ptrs, 7, false);
}

