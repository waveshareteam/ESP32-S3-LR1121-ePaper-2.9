#pragma once

/*
 * Fill only these macros when a real STA test is required. The SoftAP always
 * starts with the default test network. Do not commit real credentials.
 */
#define TEST_WIFI_STA_SSID       "JSBPI"
#define TEST_WIFI_STA_PASSWORD   "waveshare0755"

#define TEST_WIFI_AP_SSID       "LR1121-S3-TEST"
#define TEST_WIFI_AP_PASSWORD   "lr1121test"
#define TEST_WIFI_AP_CHANNEL    6
#define TEST_WIFI_AP_MAX_CONN   4

#define TEST_DEVICE_ID           "S3-LR1121-XF"

/* Regional radio test profile; confirm local regulations before radiating. */
#define TEST_LORA_FREQUENCY_MHZ      868.0f
#define TEST_LORA_BANDWIDTH_KHZ      125.0f
#define TEST_LORA_SPREADING_FACTOR   7
#define TEST_LORA_CODING_RATE        5
#define TEST_LORA_SYNC_WORD          0x12
#define TEST_LORA_OUTPUT_POWER_DBM   22
#define TEST_LORA_PREAMBLE_LENGTH    8
#define TEST_LORA_TCXO_VOLTAGE       3.0f

#define TEST_BUTTON_DEBOUNCE_MS      40
#define TEST_GPIO0_DOUBLE_CLICK_MS   600
#define TEST_BUTTON_RELEASE_MS       100
#define TEST_UART_STATUS_PERIOD_MS   30000
#define TEST_EPAPER_STATUS_PERIOD_MS 300000

#define TEST_LORA_CONFIG_INIT() { \
    .frequency_mhz = TEST_LORA_FREQUENCY_MHZ, \
    .bandwidth_khz = TEST_LORA_BANDWIDTH_KHZ, \
    .spreading_factor = TEST_LORA_SPREADING_FACTOR, \
    .coding_rate = TEST_LORA_CODING_RATE, \
    .sync_word = TEST_LORA_SYNC_WORD, \
    .output_power_dbm = TEST_LORA_OUTPUT_POWER_DBM, \
    .preamble_len = TEST_LORA_PREAMBLE_LENGTH, \
    .tcxo_voltage = TEST_LORA_TCXO_VOLTAGE, \
}
