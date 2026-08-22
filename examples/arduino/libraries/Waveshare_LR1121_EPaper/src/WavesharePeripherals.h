#pragma once

#include "WaveshareBoard.h"

namespace waveshare {

struct BatteryData {
  uint16_t voltageMv;
  float currentMa;
  int8_t temperatureC;
  uint8_t stateOfCharge;
  uint16_t timeToEmptyMinutes;
  uint16_t timeToFullMinutes;
};

class AXP2602 {
 public:
  static constexpr uint8_t kAddress = 0x62;
  bool begin();
  bool read(BatteryData &data);
  bool sleep();
  const char *lastError() const { return lastError_; }

 private:
  const char *lastError_ = "not initialized";
  bool readRegister(uint8_t reg, uint8_t &value);
  bool readBe16(uint8_t reg, uint16_t &value);
};

struct RtcTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t weekday;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  bool voltageLow;
};

class PCF85063A {
 public:
  static constexpr uint8_t kAddress = 0x51;
  bool begin();
  bool read(RtcTime &time);
  bool set(const RtcTime &time);
  const char *lastError() const { return lastError_; }

 private:
  const char *lastError_ = "not initialized";
  static uint8_t fromBcd(uint8_t value);
  static uint8_t toBcd(uint8_t value);
};

}  // namespace waveshare

