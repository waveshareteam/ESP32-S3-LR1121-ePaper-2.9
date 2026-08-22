#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WavesharePeripherals.h>

using namespace waveshare;

PCF85063A rtc;
Epaper display;

bool invalidTime(const RtcTime &time) {
  return time.voltageLow || time.year < 2024 || time.month == 0 ||
         time.month > 12 || time.day == 0 || time.day > 31;
}

void setup() {
  Serial.begin(115200);
  beginBoard();
  if (!rtc.begin() || !display.begin()) {
    Serial.println("ERROR: RTC or e-paper initialization failed");
    return;
  }
  RtcTime now = {};
  if (!rtc.read(now) || invalidTime(now)) {
    const RtcTime initial = {2026, 1, 1, 4, 12, 0, 0, false};
    if (!rtc.set(initial)) Serial.printf("RTC ERROR: %s\n", rtc.lastError());
    else Serial.println("RTC invalid; set default 2026-01-01 12:00:00");
  }
}

void loop() {
  RtcTime now = {};
  if (!rtc.read(now)) {
    Serial.printf("RTC ERROR: %s\n", rtc.lastError());
    delay(1000);
    return;
  }
  char date[24], clock[24], irq[24];
  snprintf(date, sizeof(date), "%04u-%02u-%02u W%u", now.year, now.month,
           now.day, now.weekday);
  snprintf(clock, sizeof(clock), "%02u:%02u:%02u", now.hour, now.minute,
           now.second);
  snprintf(irq, sizeof(irq), "IRQ GPIO7=%d", digitalRead(Pins::kRtcIrq));
  Serial.printf("RTC %s %s voltage_low=%s\n", date, clock,
                now.voltageLow ? "yes" : "no");
  const char *lines[] = {date, clock, irq, "PCF85063A"};
  if (!display.showLines("RTC", lines, 4))
    Serial.printf("EPAPER ERROR: %s\n", display.lastError());
  delay(1000);
}

