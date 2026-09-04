#include <esp_sleep.h>

#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WavesharePeripherals.h>
#include <WaveshareRadio.h>

using namespace waveshare;

Epaper display;
AXP2602 battery;
LR1121BoardRadio boardRadio;

void setup() {
  Serial.begin(115200);
  beginBoard();
  display.begin();
  battery.begin();
  const int16_t radioState = boardRadio.begin();
  Serial.printf("wake cause=%d radio=%d\n", esp_sleep_get_wakeup_cause(),
                radioState);
  const char *lines[] = {"TIMER 30 SECONDS", "BOOT KEY GPIO0",
                         "RADIO SLEEP FIRST"};
  display.showLines("SLEEP", lines, 3, true);
  esp_sleep_enable_timer_wakeup(30ULL * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(Pins::kBoot), 0);
  if (radioState == RADIOLIB_ERR_NONE) boardRadio.sleep();
  battery.sleep();
  display.sleep();
  pinMode(Pins::kRadioCs, OUTPUT);
  digitalWrite(Pins::kRadioCs, HIGH);
  Serial.println("start deep sleep");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {}

