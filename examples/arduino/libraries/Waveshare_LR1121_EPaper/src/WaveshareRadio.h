#pragma once

#include "WaveshareBoard.h"

#include <RadioLib.h>

namespace waveshare {

// LoRaWAN reapplies power through the base API, so both virtual paths must
// keep this board on the LR1121 high-power PA.
class HighPowerLR1121 final : public LR1121 {
 public:
  explicit HighPowerLR1121(Module *module) : LR1121(module) {}

  using LR1120::checkOutputPower;
  using LR1120::setOutputPower;

  int16_t setOutputPower(int8_t power) override {
    return LR1120::setOutputPower(power, true);
  }

  int16_t checkOutputPower(int8_t power, int8_t *clipped) override {
    return LR1120::checkOutputPower(power, clipped, true);
  }
};

class LR1121BoardRadio {
 public:
  LR1121BoardRadio();
  int16_t begin(float frequencyMhz = 868.0f, float bandwidthKhz = 125.0f,
                uint8_t spreadingFactor = 7, uint8_t codingRate = 5,
                int8_t powerDbm = 22, uint16_t preambleLength = 8,
                uint8_t syncWord = RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE);
  int16_t beginLoRaWAN(float frequencyMhz, int8_t powerDbm = 22);
  LR1121 &radio() { return radio_; }
  int16_t sleep();

 private:
  Module module_;
  HighPowerLR1121 radio_;
  void installRfSwitch();
};

}  // namespace waveshare

