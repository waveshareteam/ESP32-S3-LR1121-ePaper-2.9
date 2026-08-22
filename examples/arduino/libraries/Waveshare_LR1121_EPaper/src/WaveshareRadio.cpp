#include "WaveshareRadio.h"

namespace waveshare {

namespace {
const uint32_t kRfSwitchPins[] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6,
    RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};

const Module::RfSwitchMode_t kRfSwitchTable[] = {
    {LR11x0::MODE_STBY, {LOW, LOW}},
    {LR11x0::MODE_RX, {HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, HIGH}},
    {LR11x0::MODE_TX_HP, {LOW, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, LOW}},
    {LR11x0::MODE_GNSS, {LOW, LOW}},
    {LR11x0::MODE_WIFI, {LOW, LOW}},
    END_OF_MODE_TABLE};
}

LR1121BoardRadio::LR1121BoardRadio()
    : module_(Pins::kRadioCs, Pins::kRadioDio1, Pins::kRadioReset,
              Pins::kRadioBusy, SPI,
              SPISettings(8000000, MSBFIRST, SPI_MODE0)),
      radio_(&module_) {}

void LR1121BoardRadio::installRfSwitch() {
  radio_.setRfSwitchTable(kRfSwitchPins, kRfSwitchTable);
}

int16_t LR1121BoardRadio::begin(float frequencyMhz, float bandwidthKhz,
                                uint8_t spreadingFactor, uint8_t codingRate,
                                int8_t powerDbm, uint16_t preambleLength,
                                uint8_t syncWord) {
  const int16_t state = radio_.begin(frequencyMhz, bandwidthKhz,
                                     spreadingFactor, codingRate, syncWord,
                                     powerDbm, preambleLength, 3.0f);
  if (state == RADIOLIB_ERR_NONE) installRfSwitch();
  return state;
}

int16_t LR1121BoardRadio::beginLoRaWAN(float frequencyMhz, int8_t powerDbm) {
  const int16_t state = radio_.begin(
      frequencyMhz, 125.0f, 7, 5, RADIOLIB_LORAWAN_LORA_SYNC_WORD,
      powerDbm, RADIOLIB_LORAWAN_LORA_PREAMBLE_LEN, 3.0f);
  if (state == RADIOLIB_ERR_NONE) installRfSwitch();
  return state;
}

int16_t LR1121BoardRadio::sleep() { return radio_.sleep(); }

}  // namespace waveshare

