#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "WaveshareConfig.h"

namespace waveshare {

enum class EpaperModel : uint8_t { Epaper213 = 213, Epaper290 = 29 };

#if !defined(WAVESHARE_EPAPER_213) && !defined(WAVESHARE_EPAPER_290)
#define WAVESHARE_EPAPER_290
#endif

#if defined(WAVESHARE_EPAPER_213) && defined(WAVESHARE_EPAPER_290)
#error "Select only one e-paper model"
#endif

struct Pins {
  static constexpr uint8_t kBoot = 0;
  static constexpr uint8_t kRadioButton = 1;
  static constexpr uint8_t kRadioDio1 = 6;
  static constexpr uint8_t kRtcIrq = 7;
  static constexpr uint8_t kEpaperBusy = 8;
  static constexpr uint8_t kEpaperReset = 9;
  static constexpr uint8_t kSdCmd = 10;
  static constexpr uint8_t kSdD0 = 11;
  static constexpr uint8_t kSdClk = 12;
  static constexpr uint8_t kI2cSda = 13;
  static constexpr uint8_t kI2cScl = 14;
  static constexpr uint8_t kEpaperDc = 17;
  static constexpr uint8_t kEpaperCs = 18;
  static constexpr uint8_t kWs2812 = 21;
  static constexpr uint8_t kSharedPower = 38;
  static constexpr uint8_t kRadioReset = 39;
  static constexpr uint8_t kSpiSck = 40;
  static constexpr uint8_t kRadioBusy = 41;
  static constexpr uint8_t kRadioCs = 42;
  static constexpr uint8_t kSpiMosi = 45;
  static constexpr uint8_t kSpiMiso = 46;
  static constexpr uint8_t kChargeStatus = 47;
  static constexpr uint8_t kAxpIrq = 48;
};

struct PanelInfo {
  EpaperModel model;
  const char *name;
  uint16_t visibleWidth;
  uint16_t ramWidth;
  uint16_t height;
};

const PanelInfo &selectedPanel();
void beginBoard(bool startI2c = true, bool startSpi = true);
void setSharedPower(bool enabled);
void setRgb(uint8_t red, uint8_t green, uint8_t blue);
bool i2cProbe(uint8_t address);
bool i2cRead(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
bool i2cWrite(uint8_t address, uint8_t reg, uint8_t value);

}  // namespace waveshare

