#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WavesharePeripherals.h>

using namespace waveshare;

AXP2602 battery;
Epaper display;

void setup() {
  Serial.begin(115200);
  beginBoard();
  if (!battery.begin()) Serial.printf("AXP2602 ERROR: %s\n", battery.lastError());
  if (!display.begin()) Serial.printf("EPAPER ERROR: %s\n", display.lastError());
}

void loop() {
  BatteryData value = {};
  if (!battery.read(value)) {
    Serial.printf("AXP2602 ERROR: %s\n", battery.lastError());
    delay(3000);
    return;
  }
  Serial.printf("battery: %u mV %.1f mA %u%% %d C TTE=%u TTF=%u min\n",
                value.voltageMv, value.currentMa, value.stateOfCharge,
                value.temperatureC, value.timeToEmptyMinutes,
                value.timeToFullMinutes);
  char line1[32], line2[32], line3[32];
  snprintf(line1, sizeof(line1), "%u mV  %.1f mA", value.voltageMv,
           value.currentMa);
  snprintf(line2, sizeof(line2), "SOC %u%%  TEMP %d C",
           value.stateOfCharge, value.temperatureC);
  snprintf(line3, sizeof(line3), "TTE %u  TTF %u MIN",
           value.timeToEmptyMinutes, value.timeToFullMinutes);
  const char *lines[] = {line1, line2, line3};
  if (!display.showLines("AXP2602", lines, 3))
    Serial.printf("EPAPER ERROR: %s\n", display.lastError());
  delay(5000);
}

