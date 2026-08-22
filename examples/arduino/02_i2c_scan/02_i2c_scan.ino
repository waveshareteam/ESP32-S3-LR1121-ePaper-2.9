#include <WaveshareBoard.h>

using namespace waveshare;

const char *deviceName(uint8_t address) {
  if (address == 0x51) return "PCF85063A";
  if (address == 0x62) return "AXP2602";
  return "unknown";
}

void setup() {
  Serial.begin(115200);
  beginBoard(true, false);
  Serial.println("I2C scan: SDA=GPIO13 SCL=GPIO14 400 kHz");
}

void loop() {
  uint8_t found = 0;
  for (uint8_t address = 0x03; address <= 0x77; ++address) {
    if (!i2cProbe(address)) continue;
    Serial.printf("found 0x%02X %s\n", address, deviceName(address));
    ++found;
  }
  Serial.printf("scan done, found %u device(s)\n\n", found);
  delay(5000);
}

