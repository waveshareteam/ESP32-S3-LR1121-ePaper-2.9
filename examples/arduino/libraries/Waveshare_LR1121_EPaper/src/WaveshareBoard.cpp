#include "WaveshareBoard.h"

#include <esp32-hal-rgb-led.h>

namespace waveshare {

namespace {
#if defined(WAVESHARE_EPAPER_213)
constexpr PanelInfo kPanel = {
    EpaperModel::Epaper213, "2.13-inch SSD16xx", 122, 128, 250};
#else
constexpr PanelInfo kPanel = {
    EpaperModel::Epaper290, "2.9-inch SSD16xx", 128, 128, 296};
#endif
}

const PanelInfo &selectedPanel() { return kPanel; }

void setSharedPower(bool enabled) {
  pinMode(Pins::kSharedPower, OUTPUT);
  digitalWrite(Pins::kSharedPower, enabled ? HIGH : LOW);
  delay(enabled ? 100 : 10);
}

void beginBoard(bool startI2c, bool startSpi) {
  setSharedPower(true);
  pinMode(Pins::kBoot, INPUT_PULLUP);
  pinMode(Pins::kRadioButton, INPUT_PULLUP);
  pinMode(Pins::kEpaperCs, OUTPUT);
  pinMode(Pins::kRadioCs, OUTPUT);
  digitalWrite(Pins::kEpaperCs, HIGH);
  digitalWrite(Pins::kRadioCs, HIGH);
  if (startI2c) {
    Wire.begin(Pins::kI2cSda, Pins::kI2cScl, 400000);
  }
  if (startSpi) {
    SPI.begin(Pins::kSpiSck, Pins::kSpiMiso, Pins::kSpiMosi);
  }
}

void setRgb(uint8_t red, uint8_t green, uint8_t blue) {
  rgbLedWrite(Pins::kWs2812, red, green, blue);
}

bool i2cProbe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
  if (data == nullptr || length == 0) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, length) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool i2cWrite(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

}  // namespace waveshare

