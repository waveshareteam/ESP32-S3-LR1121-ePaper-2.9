#include "WavesharePeripherals.h"

namespace waveshare {

bool AXP2602::readRegister(uint8_t reg, uint8_t &value) {
  return i2cRead(kAddress, reg, &value, 1);
}

bool AXP2602::readBe16(uint8_t reg, uint16_t &value) {
  uint8_t bytes[2] = {};
  if (!i2cRead(kAddress, reg, bytes, sizeof(bytes))) return false;
  value = (static_cast<uint16_t>(bytes[0]) << 8U) | bytes[1];
  return true;
}

bool AXP2602::begin() {
  uint8_t chipId = 0;
  if (!readRegister(0x00, chipId) || (chipId & 0x3FU) != 0x1CU) {
    lastError_ = "AXP2602 ID mismatch";
    return false;
  }
  uint8_t mode = 0;
  if (!readRegister(0x02, mode) || !i2cWrite(kAddress, 0x02, mode & ~0x01U)) {
    lastError_ = "AXP2602 wake failed";
    return false;
  }
  uint8_t config = 0;
  if (!readRegister(0x11, config) ||
      !i2cWrite(kAddress, 0x11, config | 0x70U)) {
    lastError_ = "AXP2602 measurement setup failed";
    return false;
  }
  delay(350);
  lastError_ = "none";
  return true;
}

bool AXP2602::read(BatteryData &data) {
  uint16_t voltage = 0, current = 0, empty = 0, full = 0;
  uint8_t temperature = 0, soc = 0;
  if (!readBe16(0x04, voltage) || !readRegister(0x06, temperature) ||
      !readRegister(0x08, soc) || !readBe16(0x0A, empty) ||
      !readBe16(0x0C, full) || !readBe16(0x14, current)) {
    lastError_ = "AXP2602 read failed";
    return false;
  }
  voltage &= 0x3FFFU;
  if (voltage > 4500U || soc > 100U) {
    lastError_ = "AXP2602 data out of range";
    return false;
  }
  data.voltageMv = voltage;
  data.currentMa = static_cast<int16_t>(current) * 0.25f;
  data.temperatureC = static_cast<int8_t>(temperature);
  data.stateOfCharge = soc;
  data.timeToEmptyMinutes = empty;
  data.timeToFullMinutes = full;
  lastError_ = "none";
  return true;
}

bool AXP2602::sleep() {
  uint8_t mode = 0;
  if (!readRegister(0x02, mode) || !i2cWrite(kAddress, 0x02, mode | 0x01U)) {
    lastError_ = "AXP2602 sleep failed";
    return false;
  }
  return true;
}

uint8_t PCF85063A::fromBcd(uint8_t value) {
  return static_cast<uint8_t>((value >> 4U) * 10U + (value & 0x0FU));
}

uint8_t PCF85063A::toBcd(uint8_t value) {
  return static_cast<uint8_t>((value / 10U) << 4U | value % 10U);
}

bool PCF85063A::begin() {
  if (!i2cProbe(kAddress)) {
    lastError_ = "PCF85063A not found";
    return false;
  }
  lastError_ = "none";
  return true;
}

bool PCF85063A::read(RtcTime &time) {
  uint8_t registers[7] = {};
  if (!i2cRead(kAddress, 0x04, registers, sizeof(registers))) {
    lastError_ = "PCF85063A read failed";
    return false;
  }
  time.voltageLow = (registers[0] & 0x80U) != 0;
  time.second = fromBcd(registers[0] & 0x7FU);
  time.minute = fromBcd(registers[1] & 0x7FU);
  time.hour = fromBcd(registers[2] & 0x3FU);
  time.day = fromBcd(registers[3] & 0x3FU);
  time.weekday = registers[4] & 0x07U;
  time.month = fromBcd(registers[5] & 0x1FU);
  time.year = 2000U + fromBcd(registers[6]);
  lastError_ = "none";
  return true;
}

bool PCF85063A::set(const RtcTime &time) {
  if (time.year < 2000 || time.year > 2099 || time.month < 1 ||
      time.month > 12 || time.day < 1 || time.day > 31 || time.hour > 23 ||
      time.minute > 59 || time.second > 59) {
    lastError_ = "RTC time is invalid";
    return false;
  }
  const uint8_t values[] = {toBcd(time.second), toBcd(time.minute),
                            toBcd(time.hour), toBcd(time.day),
                            static_cast<uint8_t>(time.weekday & 0x07U),
                            toBcd(time.month), toBcd(time.year - 2000U)};
  Wire.beginTransmission(kAddress);
  Wire.write(0x04);
  Wire.write(values, sizeof(values));
  if (Wire.endTransmission() != 0) {
    lastError_ = "PCF85063A write failed";
    return false;
  }
  lastError_ = "none";
  return true;
}

}  // namespace waveshare

