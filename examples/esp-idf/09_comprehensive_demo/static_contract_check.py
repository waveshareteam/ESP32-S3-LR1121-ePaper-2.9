#!/usr/bin/env python3
"""Source-only contracts for the comprehensive board test.

This intentionally does not invoke ESP-IDF, CMake, Ninja, or hardware tools.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parent
FAILURES = []


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(path, token, description):
    if token not in read(path):
        FAILURES.append(f"{description}: missing {token!r} in {path}")


def forbid(path, token, description):
    if token in read(path):
        FAILURES.append(f"{description}: forbidden {token!r} in {path}")


def forbid_regex(path, pattern, description):
    if re.search(pattern, read(path)):
        FAILURES.append(f"{description}: forbidden pattern {pattern!r} in {path}")


def require_order(path, tokens, description):
    text = read(path)
    positions = [text.find(token) for token in tokens]
    if any(position < 0 for position in positions):
        missing = [token for token, position in zip(tokens, positions)
                   if position < 0]
        FAILURES.append(f"{description}: missing {missing} in {path}")
    elif positions != sorted(positions):
        FAILURES.append(f"{description}: wrong order in {path}")


for state in (
    "ACTIVE_SELF_TEST",
    "ACTIVE_READY",
    "LORA_TX",
    "LORA_RX",
    "PREPARE_SLEEP",
    "LOW_POWER",
    "WAKE_REINIT",
):
    require("main/app_tests.h", state, "runtime state contract")

for token in (
    "filter_sample(&gpio1, now, &pressed) && pressed",
    "filter_sample(&gpio0, now, &pressed) && !pressed",
    "TEST_GPIO0_DOUBLE_CLICK_MS",
    "APP_EVENT_BUTTON_GPIO1",
    "GPIO_INTR_ANYEDGE",
):
    require("main/app_buttons.c", token, "button contract")
forbid("main/app_buttons.c", "return !pressed;",
       "GPIO1 must trigger on stable press, not release")
require_order(
    "main/app_buttons.c",
    (
        "/* Prevent either ISR from observing s_task after the task exits. */\n"
        "    (void)gpio_intr_disable(BOARD_BUTTON_SLEEP_GPIO);\n"
        "    (void)gpio_intr_disable(BOARD_BUTTON_RADIO_GPIO);",
        "atomic_store_explicit(&s_stop, true, memory_order_release);",
        "EventBits_t bits = xEventGroupWaitBits(",
        "    remove_isr_handlers();\n    s_task = NULL;",
    ),
    "button ISR shutdown order",
)

for token in (
    "RADIO_COMMAND_TX",
    "RADIO_COMMAND_STOP",
    "esp_crc32_le",
    "TEST_LORA_CONFIG_INIT",
    "receive_once(status, 1)",
    "xQueueSendToFront(s_events, &event, pdMS_TO_TICKS(250))",
):
    require("main/app_radio_service.c", token, "LoRa service contract")
forbid(
    "main/app_radio_service.c",
    "APP_EVENT_RADIO_RX_PACKET",
    "unconsumed RX notifications must not fill the runtime event queue",
)
require(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_radio.cpp",
    "static void IRAM_ATTR radio_operation_done(void)",
    "LR1121 callback must match the shared IRAM GPIO ISR service",
)
for token in (
    "static esp_err_t gpio_hold_dis_checked(gpio_num_t gpio_num);",
    "ESP_RETURN_ON_ERROR(gpio_hold_dis_checked(radio_pins[i]), TAG,",
):
    require(
        "components/esp32_s3_lr1121_epaper_2_9_support/src/board_radio.cpp",
        token,
        "LR1121 wake hold release contract",
    )
forbid(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_radio.cpp",
    "gpio_hold_dis_if_supported",
    "stale LR1121 GPIO hold helper declaration",
)

for token in (
    "WIFI_MODE_APSTA",
    "WIFI_PS_NONE",
    "esp_netif_destroy_default_wifi",
    "esp_event_loop_delete_default",
    "#if CONFIG_ESP_NETIF_TCPIP_LWIP",
    "global lwIP core remains initialized",
):
    require("main/app_wifi.c", token, "Wi-Fi lifecycle contract")
require_order(
    "main/app_wifi.c",
    (
        "esp_netif_destroy_default_wifi(s_ap_netif)",
        "esp_netif_destroy_default_wifi(s_sta_netif)",
        "esp_event_loop_delete_default()",
        "#if CONFIG_ESP_NETIF_TCPIP_LWIP",
    ),
    "Wi-Fi owned resource cleanup order",
)
for token in (
    "if (s_ip_handler_registered || s_wifi_handler_registered)",
    "if (s_wifi_initialized || s_ap_netif || s_sta_netif ||",
    "!s_event_loop_owned && !s_netif_owned",
):
    require("main/app_wifi.c", token, "Wi-Fi retryable cleanup contract")

for token in (
    "ble_gap_disc",
    "BLE_HS_FOREVER",
    ".passive = 1",
    "nimble_port_freertos_deinit",
    "nimble_port_stop",
    "nimble_port_deinit",
    "s_host_stopped",
):
    require("main/app_ble.c", token, "BLE lifecycle contract")
require_order(
    "main/app_ble.c",
    ("nimble_port_stop()", "nimble_port_deinit()"),
    "BLE official stop/deinit order",
)
forbid("main/app_ble.c", "BLE_HOST_STOPPED",
       "NimBLE task self-delete must not leave unreachable completion code")

for path in ("main/app_runtime.c", "main/CMakeLists.txt", "main/app_tests.h"):
    forbid(path, "app_stress", "continuous stress testing is removed")

require_order(
    "main/app_runtime.c",
    (
        "app_buttons_stop()",
        "app_storage_stop()",
        "app_battery_monitor_stop()",
        "app_ble_stop()",
        "app_wifi_stop()",
        "app_radio_service_stop()",
    ),
    "active task stop order",
)
require_order(
    "main/app_runtime.c",
    (
        "test_sleep_abort();",
        "cleanup_partial_active_state();",
        "wait_for_gpio0_release();",
    ),
    "wake hold release order",
)
require_order(
    "main/test_sleep.cpp",
    (
        "board_radio_prepare_deep_sleep()",
        "board_epaper_sleep()",
        "board_status_led_set(false)",
        "board_rtc_stop_timer_interrupt()",
        "app_battery_prepare_sleep()",
        "board_epaper_deinit()",
        "board_support_deinit()",
        "board_prepare_gpio_deep_sleep()",
        "board_radio_isolate_powered_off_pins()",
    ),
    "peripheral shutdown order",
)

for token in (
    "esp_sleep_enable_ext1_wakeup_io",
    "ESP_EXT1_WAKEUP_ANY_LOW",
    "BIT64(BOARD_BUTTON_SLEEP_GPIO)",
    "rtc_gpio_pullup_dis(BOARD_BUTTON_SLEEP_GPIO)",
    "esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF)",
):
    require("main/test_sleep.cpp", token, "GPIO0 wake contract")
for token in (
    "esp_sleep_enable_gpio_wakeup",
    "esp_sleep_enable_ext0_wakeup",
    "nvs_flash_erase",
):
    forbid("main/app_runtime.c", token, "destructive or wrong wake contract")
    forbid("main/test_sleep.cpp", token, "destructive or wrong wake contract")

for token in (
    "BOARD_RADIO_DIO1_GPIO",
    "BOARD_RADIO_NSS_GPIO",
    "BOARD_RADIO_RESET_GPIO",
    "BOARD_RADIO_BUSY_GPIO",
    "BOARD_SHARED_SPI_SCK_GPIO",
    "BOARD_SHARED_SPI_MISO_GPIO",
    "BOARD_SHARED_SPI_MOSI_GPIO",
    "GPIO_MODE_DISABLE",
    "gpio_hold_en_checked(gpio_num)",
):
    require(
        "components/esp32_s3_lr1121_epaper_2_9_support/src/board_radio.cpp",
        token,
        "powered-off LR1121 isolation contract",
    )
forbid(
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_radio.h",
    "board_radio_hold_sleep_pins",
    "unsafe powered-off LR1121 pin contract",
)

for token in (
    "format_if_mount_failed = false",
    "sdmmc_read_sectors",
    "SD_TEST_BYTES",
    "slot.width = 1",
):
    require("main/test_storage.c", token, "read-only SD contract")
driver_path = "components/esp_epaper/src/controllers/ssd16xx.c"
driver_text = read(driver_path)
fast_match = re.search(
    r"static esp_err_t ssd16xx_update_fast\(.*?\n}\n",
    driver_text,
    flags=re.DOTALL,
)
if fast_match is None:
    FAILURES.append(f"SSD16xx fast-refresh function missing in {driver_path}")
else:
    fast_body = fast_match.group(0)
    for token in (
        "CMD(spi, 0x11);",
        "CMD(spi, 0x44);",
        "CMD(spi, 0x45);",
        "CMD(spi, 0x4E);",
        "CMD(spi, 0x4F);",
    ):
        if token not in fast_body:
            FAILURES.append(
                f"SSD16xx fast-refresh cursor contract: missing {token!r}"
            )
    for token in ("RESET(spi);", "CMD(spi, 0x12);"):
        if token in fast_body:
            FAILURES.append(
                f"SSD16xx fast-refresh reset regression: forbidden {token!r}"
            )
require(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c",
    "configure_sleep_output(BOARD_SDMMC_CLK_GPIO, 0)",
    "SD clock deep-sleep hold contract",
)
require("main/test_led.c", "board_status_led_set(true)",
        "active WS2812 load contract")
require(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
    "else {\n                s_panel.epd = NULL;\n            }",
    "retain e-paper handle when deinit fails",
)
require(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
    "if (!s_panel.epd) {\n        esp_err_t power_ret = board_epaper_power_set(false);",
    "power off e-paper only after component deinit releases its handle",
)
forbid(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
    "if (ret == ESP_OK) {\n        power_ret = board_epaper_power_set(false);",
    "controller sleep must not cut power before epd_deinit",
)
for token in (
    "BOARD_EPAPER_FAST_REFRESH_LIMIT 10U",
    "board_epaper_select_update_mode",
    "EPD_UPDATE_FAST",
    "s_fast_refreshes_remaining",
    "lv_font_terminus_14_bold",
):
    require(
        "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
        token,
        "e-paper fast-refresh and 1-bpp font contract",
    )
forbid(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
    "epd_flush_framebuffer(s_panel.epd, EPD_UPDATE_FULL)",
    "LVGL flush must honor the selected full/fast update mode",
)
require(
    "components/epaper_ui_font/CMakeLists.txt",
    '"src/lv_font_terminus_14_bold.c"',
    "Terminus font source registration",
)
for token in (
    "ASCII U+0020-U+007E, 1 bit per pixel",
    ".range_start = 0x20",
    ".range_length = 0x5F",
    ".bpp = 1",
    ".line_height = 14",
):
    require(
        "components/epaper_ui_font/src/lv_font_terminus_14_bold.c",
        token,
        "e-paper bitmap font contract",
    )
require(
    "components/epaper_ui_font/OFL.txt",
    "SIL OPEN FONT LICENSE Version 1.1",
    "Terminus font license",
)
require(
    "components/epaper_ui_font/README.md",
    "ter-u14b.bdf",
    "Terminus font source provenance",
)
require(
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c",
    "esp_err_t board_rtc_stop_timer_interrupt(void)",
    "persistent peripheral wake/sleep lifecycle",
)
support_component = "components/esp32_s3_lr1121_epaper_2_9_support"
forbid(
    f"{support_component}/CMakeLists.txt",
    "board_axp2602",
    "board support must not compile an AXP2602 wrapper",
)
forbid(
    f"{support_component}/idf_component.yml",
    "lewisxhe/sensorlib",
    "SensorLib belongs to the application component",
)
for token in ("board_axp2602_internal", "board_axp2602_register_ops"):
    forbid(f"{support_component}/src/board_support.c", token,
           "legacy Board Manager AXP2602 callback")

main_component = "main"
for token in ('"test_battery.cpp"', '"test_sleep.cpp"', "sensorlib"):
    require(f"{main_component}/CMakeLists.txt", token,
            "C++ SensorLib application dependency")
require(
    f"{main_component}/idf_component.yml",
    'lewisxhe/sensorlib: "0.4.1"',
    "pinned application SensorLib dependency",
)

sensorlib_source = f"{main_component}/test_battery.cpp"
require_order(
    sensorlib_source,
    ("#include <math.h>", "#include <GaugeAXP2602.hpp>"),
    "SensorLib fabs declaration include order",
)
for token in (
    "std::unique_ptr<GaugeAXP2602> s_battery;",
    "board_i2c_acquire(&bus)",
    "battery->begin(bus)",
    "battery->setCurrentSenseResistor",
    "battery->setBatteryDetection(true)",
    "battery->setCurrentMeasurement(true)",
    "battery->setThermalDieMeasurement(true)",
    "s_battery->refresh()",
    "s_battery->clearIRQStatus()",
    "s_battery->wakeup()",
    "s_battery->sleep()",
    "s_battery.reset();",
):
    require(sensorlib_source, token, "direct SensorLib AXP2602 contract")
require(
    sensorlib_source,
    "s_battery->sleep();\n"
    "        ret = s_battery->isSleepModeEnabled() ? ESP_OK : ESP_FAIL;\n"
    "    }\n\n"
    "    // Destroy SensorLib's I2C device before Board Manager releases the bus.\n"
    "    s_battery.reset();",
    "destroy SensorLib I2C device after gauge sleep",
)
for token in (
    '#include "axp2602.h"',
    "board_axp2602_",
    "i2c_master_transmit(",
    "i2c_master_transmit_receive(",
):
    forbid(sensorlib_source, token,
           "application must use SensorLib instead of a local AXP2602 driver")
for legacy_path in (
    f"{support_component}/src/board_axp2602.cpp",
    f"{support_component}/src/board_axp2602_internal.h",
    "../components/axp2602_sensorlib",
):
    if (ROOT / legacy_path).exists():
        FAILURES.append(f"legacy AXP2602 adapter still exists: {legacy_path}")

require_order(
    "main/test_sleep.cpp",
    ("app_battery_prepare_sleep()", "board_support_deinit()"),
    "destroy SensorLib I2C device before releasing the Board Manager bus",
)

for path in ("main/test_system.c",):
    require(path, '#include "psa/crypto.h"', "ESP-IDF 6.2 crypto API")
    forbid(path, '#include "mbedtls/sha256.h"',
           "removed ESP-IDF 6.2 public SHA header")
    forbid(path, '#include "mbedtls/aes.h"',
           "removed ESP-IDF 6.2 public AES header")
require("main/test_system.c", '#include "esp_random.h"',
        "ESP-IDF 6.2 random API")
for token in ("psa_cipher_encrypt", "psa_hash_compute"):
    require("main/test_system.c", token, "PSA crypto self-test contract")
require(
    "main/test_system.c",
    "heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)",
    "internal RAM inventory contract",
)

for token in (
    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y",
    "CONFIG_PM_ENABLE=n",
    "CONFIG_FREERTOS_USE_TICKLESS_IDLE=n",
    "CONFIG_SPIRAM_MODE_OCT=y",
    "CONFIG_SPIRAM_SPEED_80M=y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
    "CONFIG_BT_NIMBLE_ENABLED=y",
    "CONFIG_ESP_TASK_WDT_EN=y",
    "CONFIG_ESP_BROWNOUT_DET=y",
    "CONFIG_MBEDTLS_AES_C=y",
    "CONFIG_MBEDTLS_SHA256_C=y",
    "CONFIG_MBEDTLS_HARDWARE_AES=y",
    "CONFIG_MBEDTLS_HARDWARE_SHA=y",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
):
    require("sdkconfig.defaults", token, "performance and safety config")

for token in (
    '"app_ble.c"',
    '"test_storage.c"',
    "esp_driver_sdmmc",
    "fatfs",
    "spi_flash",
):
    require("main/CMakeLists.txt", token, "component source contract")
forbid_regex(
    "main/CMakeLists.txt",
    r"(?<![A-Za-z0-9_])esp_flash(?![A-Za-z0-9_])",
    "esp_flash.h belongs to the spi_flash component",
)

for token in ("R42 (0R)", "R43 未装", "Q4 (AO3401)"):
    require("README.md", token, "schematic power-path documentation")

public_suffixes = {".c", ".cpp", ".h", ".md", ".txt", ".yml", ".yaml"}
drive_path = re.compile(r"\b[A-Za-z]:[\\/]")
unc_path = re.compile(r"\\\\(?:192\.168\.|[^\\\s]+\\)")
for path in ROOT.rglob("*"):
    if not path.is_file() or path.suffix.lower() not in public_suffixes:
        continue
    if "build" in path.parts or "managed_components" in path.parts:
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    if drive_path.search(text) or unc_path.search(text):
        FAILURES.append(f"local absolute path leaked in {path.relative_to(ROOT)}")

if FAILURES:
    print("SOURCE CONTRACT CHECK: FAIL")
    for failure in FAILURES:
        print(f" - {failure}")
    sys.exit(1)

print("SOURCE CONTRACT CHECK: PASS")
print("Validated state, button, radio, Wi-Fi/BLE, SD, SensorLib AXP2602, e-paper and sleep contracts.")
