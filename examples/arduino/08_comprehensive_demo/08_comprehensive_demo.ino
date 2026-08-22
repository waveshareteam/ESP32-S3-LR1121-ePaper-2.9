#include <Preferences.h>
#include <WiFi.h>

#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WavesharePeripherals.h>
#include <WaveshareRadio.h>

using namespace waveshare;

Epaper display;
AXP2602 battery;
PCF85063A rtc;
LR1121BoardRadio boardRadio;
Preferences storage;
uint32_t screen = 0;

void setup() {
  Serial.begin(115200);
  beginBoard();
  display.begin();
  const bool batteryOk = battery.begin();
  const bool rtcOk = rtc.begin();
  const int16_t radioState = boardRadio.begin();
  storage.begin("demo08", false);
  const uint32_t boots = storage.getUInt("boots", 0) + 1;
  storage.putUInt("boots", boots);
  Serial.printf("comprehensive: battery=%s rtc=%s radio=%d boots=%lu\n",
                batteryOk ? "OK" : "FAIL", rtcOk ? "OK" : "FAIL",
                radioState, static_cast<unsigned long>(boots));
}

void loop() {
  if (digitalRead(Pins::kBoot) == LOW) {
    ++screen;
    while (digitalRead(Pins::kBoot) == LOW) delay(10);
  }
  BatteryData bat = {};
  RtcTime now = {};
  const bool batteryOk = battery.read(bat);
  const bool rtcOk = rtc.read(now);
  char line1[32], line2[32], line3[32], line4[32];
  snprintf(line1, sizeof(line1), "BAT %s %uMV %u%%",
           batteryOk ? "OK" : "ERR", bat.voltageMv, bat.stateOfCharge);
  snprintf(line2, sizeof(line2), "RTC %s %02u:%02u:%02u",
           rtcOk ? "OK" : "ERR", now.hour, now.minute, now.second);
  snprintf(line3, sizeof(line3), "I2C 51=%d 62=%d", i2cProbe(0x51),
           i2cProbe(0x62));
  snprintf(line4, sizeof(line4), "KEY=%d RADIOKEY=%d", digitalRead(0),
           digitalRead(1));
  const char *lines[] = {line1, line2, line3, line4,
                         "GPIO0: NEXT PAGE"};
  display.showLines("SYSTEM TEST", lines, 5, screen % 10 == 0);
  setRgb(0, 24, 0);
  delay(1000);
  setRgb(0, 0, 0);
}
