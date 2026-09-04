#!/usr/bin/env python3
"""Read-only contract checks for example 13. This script never builds code."""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


EXAMPLE = Path(__file__).resolve().parent
REPO = EXAMPLE.parent
RADIOLIB = EXAMPLE / "managed_components" / "jgromes__radiolib"


failures: list[str] = []
checks = 0


def read(path: Path, label: str) -> str:
    global checks
    checks += 1
    if not path.is_file():
        failures.append(f"{label}: missing file")
        return ""
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        failures.append(f"{label}: unreadable ({type(exc).__name__})")
        return ""


def expect(text: str, needle: str, label: str) -> None:
    global checks
    checks += 1
    if needle not in text:
        failures.append(f"{label}: missing {needle!r}")


def reject(text: str, pattern: str, label: str, flags: int = 0) -> None:
    global checks
    checks += 1
    if re.search(pattern, text, flags):
        failures.append(f"{label}: forbidden pattern {pattern!r}")


required = [
    "CMakeLists.txt",
    "dependencies.lock",
    "sdkconfig.defaults",
    "README.md",
    "main/CMakeLists.txt",
    "main/idf_component.yml",
    "main/main.cpp",
    "main/lorawan_profile.h",
    "main/lorawan_profile.cpp",
    "main/radio_runtime.h",
    "main/radio_runtime.cpp",
    "main/multicast_session.h",
    "main/multicast_session.cpp",
    "main/lorawan_flow.h",
    "main/lorawan_flow.cpp",
    "main/multicast_config.h",
    "main/multicast_store.h",
    "main/multicast_store.cpp",
    "main/multicast_ui.h",
    "main/multicast_ui.cpp",
    "main/lorawan_secrets.h.example",
    "components/esp32_s3_lr1121_epaper_2_9_support/CMakeLists.txt",
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_support.h",
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_lorawan_radio.h",
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_epaper.h",
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c",
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_lorawan_radio.cpp",
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
    "components/gen_bmgr_codes/CMakeLists.txt",
    "components/gen_bmgr_codes/gen_board_periph_config.c",
    "components/gen_bmgr_codes/gen_board_device_config.c",
]
for relative in required:
    read(EXAMPLE / relative, relative)

main = read(EXAMPLE / "main/main.cpp", "main state machine")
profile_h = read(EXAMPLE / "main/lorawan_profile.h", "LoRaWAN profile header")
profile_cpp = read(EXAMPLE / "main/lorawan_profile.cpp", "LoRaWAN profile source")
runtime_h = read(EXAMPLE / "main/radio_runtime.h", "radio runtime header")
runtime_cpp = read(EXAMPLE / "main/radio_runtime.cpp", "radio runtime source")
session_h = read(EXAMPLE / "main/multicast_session.h", "multicast session header")
session_cpp = read(EXAMPLE / "main/multicast_session.cpp", "multicast session source")
flow_h = read(EXAMPLE / "main/lorawan_flow.h", "LoRaWAN flow header")
flow_cpp = read(EXAMPLE / "main/lorawan_flow.cpp", "LoRaWAN flow source")
config = read(EXAMPLE / "main/multicast_config.h", "multicast config")
store_h = read(EXAMPLE / "main/multicast_store.h", "store header")
store_cpp = read(EXAMPLE / "main/multicast_store.cpp", "store source")
ui_cpp = read(EXAMPLE / "main/multicast_ui.cpp", "UI source")
main_cmake = read(EXAMPLE / "main/CMakeLists.txt", "main CMake")
manifest = read(EXAMPLE / "main/idf_component.yml", "example manifest")
readme = read(EXAMPLE / "README.md", "example README")
secrets_template = read(
    EXAMPLE / "main/lorawan_secrets.h.example", "secrets template"
)
board = read(
    EXAMPLE
    / "components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c",
    "board support",
)
radio = read(
    EXAMPLE
    / "components/esp32_s3_lr1121_epaper_2_9_support/src/board_lorawan_radio.cpp",
    "LR1121 support",
)
generated_spi = read(
    EXAMPLE / "components/gen_bmgr_codes/gen_board_periph_config.c",
    "generated SPI config",
)
generated_device = read(
    EXAMPLE / "components/gen_bmgr_codes/gen_board_device_config.c",
    "generated device config",
)

# Behavioral contracts may move between these customer-facing modules, while
# the strict uplink sequence remains owned by lorawan_flow.cpp.
behavior_sources = "\n".join(
    (main, profile_cpp, runtime_cpp, session_cpp, flow_cpp)
)

# Keep the entry point readable for customers. Complexity belongs in named
# modules with narrow interfaces, not in another monolithic main.cpp.
checks += 1
main_line_count = len(main.splitlines())
if main_line_count > 300:
    failures.append(
        f"main.cpp readability: {main_line_count} lines exceeds 300"
    )

# Check only handwritten C/C++ sources. Generated Board Manager files and the
# local secrets header are intentionally absent from this allowlist.
handwritten_comment_files = (
    "main/main.cpp",
    "main/lorawan_profile.h",
    "main/lorawan_profile.cpp",
    "main/radio_runtime.h",
    "main/radio_runtime.cpp",
    "main/multicast_session.h",
    "main/multicast_session.cpp",
    "main/lorawan_flow.h",
    "main/lorawan_flow.cpp",
    "main/multicast_config.h",
    "main/multicast_store.h",
    "main/multicast_store.cpp",
    "main/multicast_ui.h",
    "main/multicast_ui.cpp",
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_support.h",
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_lorawan_radio.h",
    "components/esp32_s3_lr1121_epaper_2_9_support/include/board_epaper.h",
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_support.c",
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_lorawan_radio.cpp",
    "components/esp32_s3_lr1121_epaper_2_9_support/src/board_epaper.c",
)
english_comment_pattern = re.compile(
    r"(?m)^\s*(?://+|/\*+|\*+)\s*[A-Za-z]"
)
for relative in handwritten_comment_files:
    source = read(EXAMPLE / relative, f"English comments in {relative}")
    if not source:
        continue
    source_lines = len(source.splitlines())
    english_comments = len(english_comment_pattern.findall(source))
    # This is a file-level readability guard, not a per-line comment quota.
    minimum_comments = max(1, (source_lines + 159) // 160)
    checks += 1
    if english_comments < minimum_comments:
        failures.append(
            f"English comments in {relative}: found {english_comments}, "
            f"need at least {minimum_comments} for {source_lines} lines"
        )

# CMake and manifest contract.
for source in (
    "main.cpp",
    "lorawan_profile.cpp",
    "radio_runtime.cpp",
    "multicast_session.cpp",
    "lorawan_flow.cpp",
    "multicast_store.cpp",
    "multicast_ui.cpp",
):
    expect(main_cmake, f'"{source}"', f"CMake source {source}")
for component in (
    "esp_board_manager",
    "gen_bmgr_codes",
    "esp32_s3_lr1121_epaper_2_9_support",
    "esp_timer",
    "freertos",
    "jgromes__radiolib",
    "nvs_flash",
):
    expect(main_cmake, component, f"CMake component {component}")
expect(manifest, 'version: "7.7.1"', "RadioLib manifest pin")
expect(manifest, 'version: "0.6.1"', "Board Manager manifest pin")
expect(manifest, 'idf: ">=5.4.3"', "ESP-IDF manifest range")

example_lock = read(EXAMPLE / "dependencies.lock", "example dependency lock")
checks += 1
if not re.search(
    r"(?ms)^  jgromes/radiolib:.*?^    version: 7\.7\.1\s*$",
    example_lock,
):
    failures.append("example dependency lock: RadioLib is not 7.7.1")
expect(example_lock, "target: esp32s3", "example dependency target")
checks += 1
if not re.search(
    r"(?ms)^  espressif/esp_board_manager:.*?^    version: 0\.6\.1\s*$",
    example_lock,
):
    failures.append("example dependency lock: Board Manager is not 0.6.1")

# Verify API names, parameter order and implementation against the locked source.
lorawan_h = read(
    RADIOLIB / "src/protocols/LoRaWAN/LoRaWAN.h", "RadioLib LoRaWAN.h"
)
lorawan_cpp = read(
    RADIOLIB / "src/protocols/LoRaWAN/LoRaWAN.cpp", "RadioLib LoRaWAN.cpp"
)
typedef_h = read(RADIOLIB / "src/TypeDef.h", "RadioLib TypeDef.h")
official_mc = read(
    RADIOLIB
    / "examples/LoRaWAN/LoRaWAN_Multicast/LoRaWAN_Multicast.ino",
    "RadioLib multicast example",
)
for needle in (
    "#define RADIOLIB_LORAWAN_MAX_NUM_MC_GROUPS (4)",
    "int16_t startMulticastSession(uint8_t id, MulticastGroup_t* mcGroup);",
    "bool stopMulticastSession(uint8_t id, bool addRxFCnt = false);",
    "int16_t getDownlinkClassC(uint8_t* dataDown, size_t* lenDown, LoRaWANEvent_t* eventDown = NULL);",
):
    expect(lorawan_h, needle, "RadioLib multicast API")

# The profile module owns all key material and exposes only complete session
# configuration. This prevents the readability refactor from creating a public
# key-copy API or scattering key macros across customer-facing headers.
for needle in (
    "lorawan_profile_configure_multicast_group(",
    "group.cls = MULTICAST_DEVICE_CLASS",
    "group.mcAddr = MULTICAST_MC_ADDR",
    "memcpy(group.mcAppSKey, kMcAppSKey",
    "memcpy(group.mcNwkSKey, kMcNwkSKey",
    "group.mcFCnt = last_fcnt_down",
    "group.mcFCntMax = MULTICAST_MC_FCNT_MAX",
    "group.rxFCnt = 0U",
    "group.mcFreq = MULTICAST_FREQUENCY_HZ",
    "group.mcDr = MULTICAST_DATARATE",
):
    expect(profile_cpp, needle, "private multicast profile mapping")
expect(
    session_cpp,
    "lorawan_profile_configure_multicast_group(",
    "multicast profile use",
)
reject(
    profile_h + session_h + flow_h + runtime_h,
    r"(?i)(?:copy|get|read).*multicast.*key|(?:APP|NWK)_KEY_BYTES",
    "public key access API",
)
reject(
    behavior_sources,
    r"lorawan_profile_copy_multicast_keys",
    "legacy multicast key copy bridge",
)
for needle in (
    "if(mcGroup->cls == RADIOLIB_LORAWAN_CLASS_B)",
    "memcpy(&this->mcGroups[id], mcGroup, sizeof(MulticastGroup_t));",
    "event->fCnt = devFCnt32;",
    "this->mcGroups[mcGroupId].mcFCnt = devFCnt32;",
    "return(this->mcGroups[mcGroupId].rxFCnt);",
    "this->mcGroups[id] = RADIOLIB_MULTICAST_GROUP_NONE;",
    "state = this->receiveDownlink();",
    "this->fCntUp += 1;",
    "if(this->fCntUp == 0)",
    "return(this->fCntUp - 1);",
):
    expect(lorawan_cpp, needle, "RadioLib multicast implementation")
expect(official_mc, "only Multicast over Class C is implemented", "official Class C scope")
for error_name, error_value in (
    ("RADIOLIB_ERR_NETWORK_NOT_JOINED", "-1101"),
    ("RADIOLIB_ERR_DOWNLINK_MALFORMED", "-1102"),
    ("RADIOLIB_ERR_NO_RX_WINDOW", "-1105"),
):
    checks += 1
    if not re.search(
        rf"#define\s+{error_name}\s+\({error_value}\)", typedef_h
    ):
        failures.append(f"RadioLib error code mismatch: {error_name}")
checks += 1
if not re.search(
    r"state = this->transmitUplink\(.*?"
    r"state = this->receiveDownlink\(\);.*?"
    r"this->fCntUp \+= 1;",
    lorawan_cpp,
    re.S,
):
    failures.append("RadioLib TX/RX/FCntUp ordering changed")



for needle in (
    "#define BOARD_SHARED_POWER_GPIO        GPIO_NUM_38",
    "#define BOARD_POWER_STABILIZE_DELAY_MS 100",
):
    expect(board, needle, "example 13 board baseline")
expect(generated_spi, ".spi_port = SPI2_HOST", "generated SPI host")
for needle in (
    ".mosi_io_num = 45",
    ".miso_io_num = 46",
    ".sclk_io_num = 40",
):
    expect(generated_spi, needle, "generated SPI pin")
for needle in (
    ".dio1_gpio = 6",
    ".nss_gpio = 42",
    ".reset_gpio = 39",
    ".busy_gpio = 41",
    ".spi_speed_hz = 8000000",
):
    expect(generated_device, needle, "generated LR1121 setting")
for needle in (
    "HighPowerLR1121",
    "BOARD_LR1121_TCXO_VOLTAGE = 3.0f",
    "RADIOLIB_LR11X0_DIO5",
    "RADIOLIB_LR11X0_DIO6",
    "setRfSwitchTable",
):
    expect(radio, needle, "LR1121 RF configuration")

# Session boundary, Class C continuity and payload handling.
for needle in (
    "node.beginOTAA(",
    "node.activateOTAA()",
    "node.startMulticastSession(MULTICAST_GROUP_ID, &group)",
    "node.getDownlinkClassC(",
    "node.stopMulticastSession(MULTICAST_GROUP_ID, false)",
    "node.sendReceive(",
    'event.multicast ? "MULTICAST" : "UNICAST"',
    "MULTICAST_DOWNLINK_PORT",
    "LORAWAN_UNICAST_DOWNLINK_PORT",
    "RADIOLIB_LORAWAN_MAX_PAYLOAD_SIZE",
    "MULTICAST_LOG_HEX_ENABLED != 0U",
    "MULTICAST_LOG_TEXT_ENABLED != 0U",
    "runtime.begin(RadioState::kClassC)",
    "runtime.begin(RadioState::kTx)",
    "restart_multicast_until_ready(",
    "multicast_configuration_error",
    "MULTICAST RxC paused for unicast TX/RX1/RX2",
    "MULTICAST RxC restored after unicast TX/RX1/RX2",
    "bool send_receive_called = false",
    "if (send_receive_called)",
    "node.getFCntUp()",
    "RADIOLIB_ERR_DOWNLINK_MALFORMED",
    "RADIOLIB_ERR_NO_RX_WINDOW",
    "post_uplink_receive_error(state)",
):
    expect(behavior_sources, needle, "modular state/session contract")
reject(
    behavior_sources,
    r"\b(?:esp_deep_sleep|esp_light_sleep|radio\.sleep)\b",
    "Class C sleep ban",
)
reject(
    behavior_sources,
    r"sendReceive\s*\([^;]*MULTICAST_MC_ADDR",
    "multicast uplink ban",
    re.S,
)
uplink_function = re.search(
    r"LoRaWANLoopDecision\s+send_unicast_uplink\(.*?\n\}\n\n"
    r"\}\s+// namespace",
    flow_cpp,
    re.S,
)
checks += 1
if uplink_function is None:
    failures.append("unicast uplink function boundary missing")
else:
    ordered_steps = (
        (
            r"stop_multicast\(\s*runtime,\s*node,\s*multicast_active\)",
            "stop multicast",
        ),
        (
            r"const\s+uint32_t\s+fcnt_before\s*=\s*node\.getFCntUp\(\)",
            "read FCntUp before send",
        ),
        (r"node\.sendReceive\(", "send unicast"),
        (r"fcnt_after\s*=\s*node\.getFCntUp\(\)", "read FCntUp after send"),
        (r"runtime\.end\(RadioState::kIdle\)", "release radio as idle"),
        (r"if\s*\(send_receive_called\)", "test sendReceive call"),
        (
            r"persist_unicast_with_pause\(\s*runtime,\s*node,",
            "persist unicast session",
        ),
        (
            r"if\s*\(state\s*==\s*RADIOLIB_ERR_NETWORK_NOT_JOINED\)",
            "test unicast session",
        ),
        (r"restore_multicast_after_unicast\(", "restore multicast"),
        (
            r"if\s*\(post_uplink_receive_error\(state\)\)",
            "classify RX1/RX2 result",
        ),
    )
    previous_end = 0
    function_text = uplink_function.group(0)
    for pattern, step in ordered_steps:
        checks += 1
        match = re.search(pattern, function_text[previous_end:], re.S)
        if match is None:
            failures.append(f"unicast pause/save/restore order missing: {step}")
            break
        previous_end += match.end()
checks += 1
if not re.search(
    r"if \(post_uplink_receive_error\(state\)\) \{.*?"
    r"\*consecutive_failures = 0U;.*?"
    r"return LoRaWANLoopDecision::kContinue;.*?"
    r"\+\+\*consecutive_failures;",
    flow_cpp,
    re.S,
):
    failures.append("RX1/RX2 errors still reach hard TX failure counter")
checks += 1
if not re.search(
    r"if \(state == RADIOLIB_ERR_UPLINK_UNAVAILABLE\) \{\s*"
    r"\*consecutive_failures = 0U;.*?"
    r"return LoRaWANLoopDecision::kContinue;",
    flow_cpp,
    re.S,
):
    failures.append("duty-cycle defer does not break hard TX failure streak")
reject(
    flow_cpp,
    r"runtime\.end\(\*multicast_active\s*\?\s*RadioState::kClassC",
    "implicit Class C continuation after unicast",
)

# NVS schema, replay protection and ESP-IDF naming-conflict regression.
for needle in (
    'constexpr char kNvsNamespace[] = "lorawan"',
    'constexpr char kNonceKey[] = "nonce"',
    'constexpr char kSessionKey[] = "sess_sc"',
    'constexpr char kMulticastKey[] = "mc_state_v1"',
    "static_assert(sizeof(MulticastBlob) == 32U",
    "schema_version",
    "structure_length",
    "config_generation",
    "last_fcnt_down",
    "crc32",
    "event_fcnt <= blob.last_fcnt_down",
    "nvs_commit(s_store_handle)",
):
    expect(store_cpp, needle, "multicast NVS contract")
expect(store_h, "kPrerequisiteFailed", "unicast restore prerequisite")
expect(session_cpp, "const MulticastCounterState before_write = *counter", "unknown write recovery")
expect(session_cpp, "confirmed_current_write", "durable readback recovery")
reject(store_cpp, r"\bnvs_handle\s+[A-Za-z_]", "legacy nvs_handle type")
for label, pattern in (
    (
        "unicast save radio mutex",
        r"MulticastStoreResult save_unicast_once\(.*?"
        r"runtime\.begin\(RadioState::kRecovery\).*?"
        r"multicast_store_save_unicast_state\(node\).*?"
        r"multicast_store_save_unicast_nonces\(node\).*?"
        r"runtime\.end\(class_c_active \? RadioState::kClassC"
        r" : RadioState::kIdle\)",
    ),
    (
        "unicast restore radio mutex",
        r"if \(!runtime\.begin\(RadioState::kRecovery\)\) \{.*?"
        r"const UnicastRestoreReport restore =\s*"
        r"multicast_store_restore_unicast\(node\);\s*"
        r"runtime\.end\(RadioState::kIdle\);",
    ),
):
    checks += 1
    if not re.search(pattern, behavior_sources, re.S):
        failures.append(f"{label}: mutex boundary missing")

# UI contract and documentation-to-code macro mapping.
for needle in (
    "MULTICAST_DISPLAY_MIN_INTERVAL_MS",
    "xQueueOverwrite",
    '"MC%u %08" PRIX32',
    '"UNI RX %" PRIu32 "  MC RX %" PRIu32',
    '"TX %" PRIu32 "  PORT %u"',
    '"RSSI %.1f  SNR %.1f"',
):
    expect(ui_cpp, needle, "e-paper UI contract")
for macro in (
    "LORAWAN_JOIN_EUI",
    "LORAWAN_DEV_EUI",
    "LORAWAN_APP_KEY_BYTES",
    "LORAWAN_NWK_KEY_BYTES",
    "LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ",
    "LORAWAN_SINGLE_CHANNEL_DR",
    "LORAWAN_SUB_BAND",
    "LORAWAN_ADR_ENABLED",
    "LORAWAN_INITIAL_POWER_DBM",
    "LORAWAN_UPLINK_PORT",
    "LORAWAN_UNICAST_DOWNLINK_PORT",
    "LORAWAN_UPLINK_INTERVAL_SECONDS",
    "LORAWAN_JOIN_MAX_ATTEMPTS",
    "LORAWAN_JOIN_RETRY_SECONDS",
    "MULTICAST_GROUP_ID",
    "MULTICAST_MC_ADDR",
    "MULTICAST_MC_APP_S_KEY_BYTES",
    "MULTICAST_MC_NWK_S_KEY_BYTES",
    "MULTICAST_DEVICE_CLASS",
    "MULTICAST_FREQUENCY_HZ",
    "MULTICAST_DATARATE",
    "MULTICAST_DOWNLINK_PORT",
    "MULTICAST_MC_FCNT_MAX",
    "MULTICAST_CONFIG_GENERATION",
    "MULTICAST_CLEAR_STATE_ON_BOOT",
    "MULTICAST_CLASS_C_POLL_MS",
    "MULTICAST_RX_ERROR_RESTART_COUNT",
    "MULTICAST_ERROR_BACKOFF_SECONDS",
    "MULTICAST_MAX_BACKOFF_SECONDS",
    "MULTICAST_LOG_HEX_ENABLED",
    "MULTICAST_LOG_TEXT_ENABLED",
    "MULTICAST_LOG_SAFE_TEXT_LIMIT",
    "MULTICAST_DISPLAY_MIN_INTERVAL_MS",
    "MULTICAST_DISPLAY_TASK_STACK",
    "MULTICAST_DISPLAY_TASK_PRIORITY",
):
    expect(readme, f"`{macro}`", f"README macro {macro}")
for config_value, readme_value in (
    ("#define LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ 868100000UL", "868100000 Hz"),
    ("#define LORAWAN_SINGLE_CHANNEL_DR           3U", "EU868 DR3"),
    ("#define LORAWAN_SUB_BAND                    0U", "| 0 |"),
    ("#define LORAWAN_ADR_ENABLED                 0U", "0，保持 ADR 关闭"),
    ("#define LORAWAN_INITIAL_POWER_DBM           10", "10 dBm"),
    ("#define LORAWAN_UPLINK_PORT                 1U", "FPort 1"),
    ("#define LORAWAN_UNICAST_DOWNLINK_PORT       2U", "FPort 2"),
    ("#define LORAWAN_UPLINK_INTERVAL_SECONDS     60UL", "60 秒"),
    ("#define LORAWAN_JOIN_MAX_ATTEMPTS           5U", "最多 5 次"),
    ("#define LORAWAN_JOIN_RETRY_SECONDS          15UL", "15 秒"),
    ("#define MULTICAST_GROUP_ID                  0U", "slot 0"),
    ("#define MULTICAST_DEVICE_CLASS              RADIOLIB_LORAWAN_CLASS_C", "RADIOLIB_LORAWAN_CLASS_C"),
    ("#define MULTICAST_FREQUENCY_HZ              869525000UL", "869525000 Hz"),
    ("#define MULTICAST_DATARATE                  0U", "EU868 DR0"),
    ("#define MULTICAST_DOWNLINK_PORT             10U", "FPort 10"),
    ("#define MULTICAST_MC_FCNT_MAX               UINT32_MAX", "UINT32_MAX"),
    ("#define MULTICAST_CONFIG_GENERATION         1UL", "1，组配置版本"),
    ("#define MULTICAST_CLEAR_STATE_ON_BOOT       0U", "0，显式清理组播 NVS"),
    ("#define MULTICAST_CLASS_C_POLL_MS           20U", "20 ms"),
    ("#define MULTICAST_RX_ERROR_RESTART_COUNT    3U", "连续 3 次"),
    ("#define MULTICAST_ERROR_BACKOFF_SECONDS     5UL", "起点 5 秒"),
    ("#define MULTICAST_MAX_BACKOFF_SECONDS       300UL", "上限 300 秒"),
    ("#define MULTICAST_LOG_HEX_ENABLED           1U", "1，输出分段 HEX"),
    ("#define MULTICAST_LOG_TEXT_ENABLED          1U", "1，输出安全 ASCII"),
    ("#define MULTICAST_LOG_SAFE_TEXT_LIMIT       96U", "最多 96 字节"),
    ("#define MULTICAST_DISPLAY_MIN_INTERVAL_MS   5000U", "5000 ms"),
    ("#define MULTICAST_DISPLAY_TASK_STACK        8192U", "8192 字节"),
    ("#define MULTICAST_DISPLAY_TASK_PRIORITY     3U", "优先级 3"),
):
    expect(config, config_value, f"configuration value {config_value}")
    expect(readme, readme_value, f"README value {readme_value}")
for statement in (
    "OTAA + Class A",
    "静态配置 + Class C",
    "Remote Multicast Setup Package",
    "FUOTA",
    "RADIOLIB_ERR_DOWNLINK_MALFORMED",
    "RADIOLIB_ERR_NO_RX_WINDOW",
    "上行已计入",
    "未编译、未烧录",
):
    expect(readme, statement, "README scope statement")

# Placeholder and public-text hygiene. LR1120 base calls are intentional for
# LR1121 high-power PA support, so only unrelated radio models are forbidden.
for secret_macro in (
    "LORAWAN_APP_KEY_BYTES",
    "LORAWAN_NWK_KEY_BYTES",
    "MULTICAST_MC_APP_S_KEY_BYTES",
    "MULTICAST_MC_NWK_S_KEY_BYTES",
):
    expect(secrets_template, secret_macro, "secret placeholder macro")
secret_bytes = re.findall(
    r"0x([0-9A-Fa-f]{2})(?![0-9A-Fa-f])", secrets_template
)
if any(value != "00" for value in secret_bytes):
    failures.append("nonzero byte in secret template")

HYGIENE_EXCLUDED_DIRS = {
    ".vscode",
    "__pycache__",
    "build",
    "managed_components",
    "gen_bmgr_codes",
}
HYGIENE_EXCLUDED_FILES = {Path("main/lorawan_secrets.h")}

text_files = []
for root, dir_names, file_names in os.walk(EXAMPLE):
    dir_names[:] = sorted(
        name for name in dir_names if name not in HYGIENE_EXCLUDED_DIRS
    )
    root_path = Path(root)
    for file_name in sorted(file_names):
        path = root_path / file_name
        relative = path.relative_to(EXAMPLE)
        if (
            path.name == "static_contract_check.py"
            or relative in HYGIENE_EXCLUDED_FILES
        ):
            continue
        try:
            text_files.append((path, path.read_text(encoding="utf-8")))
        except (OSError, UnicodeError):
            pass
combined = "\n".join(text for _, text in text_files)
reject(combined, r"(?i)\b(?:SX1262|SX127[2689]|SX128[0123])\b", "old radio model")
reject(combined, r"(?m)^\s*void\s+(?:setup|loop)\s*\(", "Arduino entry point")
reject(combined, r"(?i)(?:[A-Z]:\\|\\\\192\.|/home/|/Users/)", "absolute local path")
reject(combined, r"(?i)(?:app|nwk|mcapp|mcnwk).*key.*(?:=|:).*0x(?!00)", "possible key literal")

if failures:
    print(f"STATIC CONTRACT CHECK FAILED: {len(failures)} issue(s)")
    for failure in failures:
        print(f"- {failure}")
    sys.exit(1)

print(f"STATIC CONTRACT CHECK PASSED: {checks} checks")
print("Scope: source/configuration inspection only; no build or hardware validation")



