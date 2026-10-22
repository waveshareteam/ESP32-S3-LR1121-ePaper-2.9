#!/usr/bin/env python3
"""Static checks only; this script never invokes a compiler or build tool."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "libraries" / "Waveshare_LR1121_EPaper"
EXPECTED = [
    "01_ws2812_led", "02_i2c_scan", "03_epaper", "04_bat_axp2602",
    "05_rtc_pcf85063", "06_lr1121_pingpong", "07_sd_card",
    "08_deep_sleep", "09_comprehensive_demo", "10_lr1121_lorawan",
    "11_LoRaWAN_ABP", "12_LoRaWAN_Class_C", "13_LoRaWAN_Multicast",
]
LORAWAN_EXAMPLES = [
    "10_lr1121_lorawan", "11_LoRaWAN_ABP", "12_LoRaWAN_Class_C",
    "13_LoRaWAN_Multicast",
]
FRAMEWORK_HEADERS = {
    "Arduino.h", "FS.h", "Preferences.h", "RadioLib.h", "SPI.h", "WiFi.h",
    "Wire.h", "cstring", "esp32-hal-rgb-led.h", "esp_sleep.h",
    "BLEDevice.h", "SD_MMC.h", "atomic", "stdint.h", "driver/gpio.h",
    "driver/rtc_io.h", "esp_crc.h", "esp_flash.h", "esp_heap_caps.h",
    "esp_random.h", "esp_timer.h", "freertos/FreeRTOS.h",
    "freertos/event_groups.h", "freertos/queue.h", "freertos/task.h",
    "mbedtls/aes.h", "mbedtls/sha256.h", "time.h",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def text(path: Path) -> str:
    require(path.is_file(), f"missing file: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def code_files() -> list[Path]:
    suffixes = {".h", ".cpp", ".ino"}
    # Inspect first-party examples and their board library, not bundled RadioLib.
    bases = [LIB / "src", *(ROOT / name for name in EXPECTED)]
    return [path for base in bases for path in base.rglob("*")
            if path.suffix in suffixes]


def check_examples() -> None:
    actual = sorted(
        p.name for p in ROOT.iterdir()
        if p.is_dir() and re.fullmatch(r"\d{2}_.+", p.name)
    )
    require(actual == sorted(EXPECTED), f"example coverage mismatch: {actual}")
    for name in EXPECTED:
        require((ROOT / name / f"{name}.ino").is_file(),
                f"missing sketch entry: {name}")


def check_dependencies_and_includes() -> None:
    metadata = text(LIB / "library.properties")
    dependency_lines = [
        line for line in metadata.splitlines() if line.startswith("depends=")
    ]
    require(dependency_lines == ["depends=RadioLib (=7.7.1)"],
            f"unexpected dependency declaration: {dependency_lines}")

    public_headers = {path.name for path in (LIB / "src").glob("*.h")}
    include_lines: list[str] = []
    for path in code_files():
        source = text(path)
        include_lines.extend(
            line for line in source.splitlines()
            if line.lstrip().startswith("#include")
        )
        for opener, header in re.findall(
                r"#include\s*([<\"])([^>\"]+)[>\"]", source):
            if opener == chr(34):
                require((path.parent / header).is_file(),
                        f"unresolved local include {header} in "
                        f"{path.relative_to(ROOT)}")
            else:
                require(header in public_headers or header in FRAMEWORK_HEADERS,
                        f"unreviewed external include {header} in "
                        f"{path.relative_to(ROOT)}")

    dependency_tree = "\n".join(dependency_lines + include_lines)
    require(re.search(r"Adafruit", dependency_tree, re.I) is None,
            "Adafruit dependency or include found")


def check_declarations_and_definitions() -> None:
    contracts = {
        "WaveshareBoard": (
            ["selectedPanel", "beginBoard", "setSharedPower", "setRgb",
             "i2cProbe", "i2cRead", "i2cWrite"],
            ["selectedPanel", "beginBoard", "setSharedPower", "setRgb",
             "i2cProbe", "i2cRead", "i2cWrite"],
        ),
        "WaveshareEPaper": (
            ["begin", "clear", "width", "height", "drawPixel", "drawLine",
             "drawRect", "drawText", "refresh", "showLines", "sleep",
             "bufferSize", "command", "data", "waitReady", "reset",
             "initializeController", "setRamWindow", "writeRam", "drawChar"],
            ["Epaper::begin", "Epaper::clear", "Epaper::width",
             "Epaper::height", "Epaper::drawPixel", "Epaper::drawLine",
             "Epaper::drawRect", "Epaper::drawText", "Epaper::refresh",
             "Epaper::showLines", "Epaper::sleep", "Epaper::bufferSize",
             "Epaper::command", "Epaper::data", "Epaper::waitReady",
             "Epaper::reset", "Epaper::initializeController",
             "Epaper::setRamWindow", "Epaper::writeRam", "Epaper::drawChar"],
        ),
        "WavesharePeripherals": (
            ["begin", "read", "sleep", "readRegister", "readBe16", "set",
             "fromBcd", "toBcd"],
            ["AXP2602::begin", "AXP2602::read", "AXP2602::sleep",
             "AXP2602::readRegister", "AXP2602::readBe16", "PCF85063A::begin",
             "PCF85063A::read", "PCF85063A::set", "PCF85063A::fromBcd",
             "PCF85063A::toBcd"],
        ),
        "WaveshareRadio": (
            ["LR1121BoardRadio", "installRfSwitch", "begin", "beginLoRaWAN",
             "sleep"],
            ["LR1121BoardRadio::LR1121BoardRadio",
             "LR1121BoardRadio::installRfSwitch", "LR1121BoardRadio::begin",
             "LR1121BoardRadio::beginLoRaWAN", "LR1121BoardRadio::sleep"],
        ),
        "WaveshareLoRaWAN": (
            ["validEui", "validKey", "nextFCntUp", "begin", "restore",
             "saveNonces", "saveSession", "saveIfCounterAdvanced", "end"],
            ["validEui", "validKey", "nextFCntUp", "LoRaWANStore::begin",
             "LoRaWANStore::restore", "LoRaWANStore::saveNonces",
             "LoRaWANStore::saveSession",
             "LoRaWANStore::saveIfCounterAdvanced", "LoRaWANStore::end"],
        ),
    }
    for stem, (declarations, definitions) in contracts.items():
        header = text(LIB / "src" / f"{stem}.h")
        source = text(LIB / "src" / f"{stem}.cpp")
        for symbol in declarations:
            require(re.search(rf"\b{re.escape(symbol)}\s*\(", header) is not None,
                    f"missing declaration: {stem}.{symbol}")
        for symbol in definitions:
            require(re.search(rf"\b{re.escape(symbol)}\s*\(", source) is not None,
                    f"missing definition: {symbol}")


def check_hardware_contract() -> None:
    board = text(LIB / "src" / "WaveshareBoard.h")
    expected_pins = {
        "kBoot": 0, "kRadioButton": 1, "kRadioDio1": 6, "kRtcIrq": 7,
        "kEpaperBusy": 8, "kEpaperReset": 9, "kSdCmd": 10,
        "kSdD0": 11, "kSdClk": 12, "kI2cSda": 13, "kI2cScl": 14,
        "kEpaperDc": 17, "kEpaperCs": 18,
        "kWs2812": 21, "kSharedPower": 38, "kRadioReset": 39,
        "kSpiSck": 40, "kRadioBusy": 41, "kRadioCs": 42,
        "kSpiMosi": 45, "kSpiMiso": 46, "kChargeStatus": 47,
        "kAxpIrq": 48,
    }
    for name, value in expected_pins.items():
        require(re.search(rf"{name}\s*=\s*{value}\s*;", board) is not None,
                f"pin mismatch: {name}")

    board_cpp = text(LIB / "src" / "WaveshareBoard.cpp")
    require(chr(34) + "2.13-inch SSD16xx" + chr(34) + ", 122, 128, 250" in board_cpp,
            "2.13 panel geometry mismatch")
    require(chr(34) + "2.9-inch SSD16xx" + chr(34) + ", 128, 128, 296" in board_cpp,
            "2.9 panel geometry mismatch")
    epaper = text(LIB / "src" / "WaveshareEPaper.cpp")
    require("digitalRead(Pins::kEpaperBusy) == HIGH" in epaper,
            "BUSY polarity mismatch")
    require("SPISettings kEpaperSpi(10000000" in epaper,
            "e-paper SPI speed mismatch")
    require("delay(10)" in epaper and "kBusyTimeoutMs" in epaper,
            "e-paper reset or timeout contract missing")
    radio = text(LIB / "src" / "WaveshareRadio.cpp")
    require("SPISettings(8000000" in radio and "3.0f" in radio,
            "LR1121 SPI speed or TCXO mismatch")
    radio_header = text(LIB / "src" / "WaveshareRadio.h")
    require("class HighPowerLR1121 final : public LR1121" in radio_header,
            "LR1121 HP adapter missing")
    require("LR1120::setOutputPower(power, true)" in radio_header,
            "LR1121 output power is not forced through the HP PA")
    require("LR1120::checkOutputPower(power, clipped, true)" in radio_header,
            "LoRaWAN power validation does not preserve the HP PA")
    require("HighPowerLR1121 radio_;" in radio_header,
            "board radio does not use the HP adapter")
    require("MODE_TX, {LOW, HIGH}" in radio.replace("    ", " "),
            "LR1121 transmit RF-switch route mismatch")


def check_credentials() -> None:
    for name in LORAWAN_EXAMPLES:
        directory = ROOT / name
        sketch = text(directory / f"{name}.ino")
        headers = list(directory.glob("*credentials.h"))
        require(len(headers) == 1, f"credential header count mismatch: {name}")
        credential_text = text(headers[0])
        require("{0}" in credential_text and "0x00000000" in credential_text,
                f"credentials are not conspicuous placeholders: {name}")
        require("provisioningError()" in sketch,
                f"missing provisioning gate: {name}")
        gate = sketch.find("provisioningError();")
        radio = sketch.find("beginLoRaWAN")
        require(0 <= gate < radio,
                f"radio can start before credential gate: {name}")
        require(re.search(r"print.*KEY|print.*EUI", sketch, re.I) is None,
                f"possible secret logging: {name}")


def check_fcnt_persistence() -> None:
    source = text(LIB / "src" / "WaveshareLoRaWAN.cpp")
    require("RADIOLIB_LORAWAN_SESSION_FCNT_UP" in source,
            "internal next FCntUp offset is not used")
    for shift in ("counter[0]", "counter[1]) << 8U",
                  "counter[2]) << 16U", "counter[3]) << 24U"):
        require(shift in source, f"next FCntUp little-endian parse missing: {shift}")
    require(
        "nextFCntUp(node) == previousNextCounter || saveSession(node)" in source,
        "counter-advanced session save path missing",
    )

    expected_calls = {
        "10_lr1121_lorawan": 1,
        "11_LoRaWAN_ABP": 1,
        "12_LoRaWAN_Class_C": 2,
    }
    for name, count in expected_calls.items():
        sketch = text(ROOT / name / f"{name}.ino")
        require(sketch.count("sendReceive(") == count,
                f"unexpected sendReceive count: {name}")
        require(sketch.count("const uint32_t before = nextFCntUp(node);") == count,
                f"missing pre-send next FCntUp snapshot: {name}")
        guarded = (
            "if (!stateStore.saveIfCounterAdvanced(node, before)) "
            "storageError();"
        )
        require(sketch.count(guarded) == count,
                f"missing fail-closed post-send persistence: {name}")


def check_multicast_safety() -> None:
    multicast = text(
        ROOT / "13_LoRaWAN_Multicast" /
        "13_LoRaWAN_Multicast.ino"
    )
    for contract in (
        "lastMulticastCounter == UINT32_MAX",
        'stateError("MULTICAST START FAILED")',
        'stateError("MC FCNT WRITE FAILED")',
    ):
        require(contract in multicast,
                f"multicast fail-closed contract missing: {contract}")


def strip_literals(source: str) -> str:
    source = re.sub(r"//.*", "", source)
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    source = re.sub(r"\"(?:\\.|[^\"\\])*\"", chr(34) * 2, source)
    source = re.sub(r"'(?:\\.|[^'\\])*'", chr(39) * 2, source)
    return source


def check_delimiters() -> None:
    pairs = {"(": ")", "[": "]", "{": "}"}
    for path in code_files():
        stack: list[str] = []
        for char in strip_literals(text(path)):
            if char in pairs:
                stack.append(char)
            elif char in pairs.values():
                require(stack and pairs[stack.pop()] == char,
                        f"delimiter mismatch: {path.relative_to(ROOT)}")
        require(not stack, f"unclosed delimiter: {path.relative_to(ROOT)}")


def check_docs_and_language() -> None:
    readme = text(ROOT / "README.md")
    for target in re.findall(r"\]\(([^)]+)\)", readme):
        if "://" in target or target.startswith("#"):
            continue
        resolved = ROOT / target.split("#", 1)[0]
        require(resolved.exists(), f"README link missing: {target}")
    for target in re.findall(r"`(examples/arduino/[^ `]+)`", readme):
        resolved = ROOT.parents[1] / target.rstrip(".,")
        require(resolved.exists(), f"README path missing: {target}")
    require("hardware validation" in readme,
            "README lacks hardware validation boundary")
    require("forceHighPower=true" in readme and "HP antenna path" in readme,
            "README lacks the default LR1121 HP path")

    han = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
    for path in code_files():
        comments = "\n".join(re.findall(r"//[^\n]*|/\*.*?\*/",
                                         text(path), flags=re.S))
        require(han.search(comments) is None,
                f"non-English code comment: {path.relative_to(ROOT)}")


def check_no_generated_outputs() -> None:
    forbidden = {"build", ".pio", ".pioenvs", ".arduinoide"}
    found = sorted(
        str(path.relative_to(ROOT)) for path in ROOT.rglob("*")
        if path.is_dir() and path.name.lower() in forbidden
    )
    require(not found, f"generated output directories found: {found}")


def main() -> None:
    checks = [
        check_examples,
        check_dependencies_and_includes,
        check_declarations_and_definitions,
        check_hardware_contract,
        check_credentials,
        check_fcnt_persistence,
        check_multicast_safety,
        check_delimiters,
        check_docs_and_language,
        check_no_generated_outputs,
    ]
    for check in checks:
        check()
        print(f"PASS {check.__name__}")
    print(f"PASS all {len(checks)} static checks (no compilation performed)")


if __name__ == "__main__":
    main()
