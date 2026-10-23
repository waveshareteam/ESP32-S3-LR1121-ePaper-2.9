#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WavesharePeripherals.h>
#include <WaveshareRadio.h>

using namespace waveshare;

constexpr uint32_t kWakeupSeconds = 600;
// Same connected radio pins as ESP-IDF 08; LR1121 DIO7/DIO8 are unconnected.
const uint8_t kRadioPins[] = {
    Pins::kRadioDio1, Pins::kRadioCs, Pins::kRadioReset, Pins::kRadioBusy,
    Pins::kSpiSck, Pins::kSpiMiso, Pins::kSpiMosi,
};

Epaper display;
AXP2602 battery;
LR1121BoardRadio boardRadio;

bool checkEsp(esp_err_t result, const char *step) {
  if (result == ESP_OK) return true;
  Serial.printf("SLEEP aborted: %s (%s); reset to retry\n", step,
                esp_err_to_name(result));
  return false;
}

bool releasePinHold(gpio_num_t pin) {
  const esp_err_t result = gpio_hold_dis(pin);
  if (result != ESP_ERR_NOT_SUPPORTED && !checkEsp(result, "release pin hold"))
    return false;
  return !rtc_gpio_is_valid_gpio(pin) ||
         checkEsp(rtc_gpio_deinit(pin), "release RTC pin");
}

void waitForBootRelease(bool rtcInput) {
  unsigned stableSamples = 0;
  while (stableSamples < 5) {
    const gpio_num_t pin = static_cast<gpio_num_t>(Pins::kBoot);
    const int level = rtcInput ? rtc_gpio_get_level(pin) : gpio_get_level(pin);
    stableSamples = level ? stableSamples + 1 : 0;
    delay(10);
  }
}

bool isolateRadioPins() {
  // Stop every SPI user before disconnecting the shared bus from the pads.
  SPI.end();
  for (uint8_t pin : kRadioPins) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (!checkEsp(gpio_config(&config), "isolate radio pin")) return false;
    const esp_err_t result = gpio_hold_en(gpio);
    // Match ESP-IDF's handling of pads without hold support (e.g. input-only).
    if (result != ESP_ERR_NOT_SUPPORTED && !checkEsp(result, "hold radio pin"))
      return false;
  }
  return true;
}

bool powerOffAndHold() {
  const gpio_num_t power = static_cast<gpio_num_t>(Pins::kSharedPower);
  if (!checkEsp(gpio_set_level(power, 0), "preload power off")) return false;
  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << Pins::kSharedPower;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  if (!checkEsp(gpio_config(&config), "configure power control") ||
      !checkEsp(gpio_set_level(power, 0), "power off") ||
      !checkEsp(gpio_hold_en(power), "hold power off")) return false;
  delay(10);
  gpio_deep_sleep_hold_en();
  return true;
}

void setup() {
  Serial.begin(115200);
  // Deep-sleep wake restarts setup(); release retained pads before any driver.
  gpio_deep_sleep_hold_dis();
  if (!releasePinHold(static_cast<gpio_num_t>(Pins::kSharedPower)) ||
      !releasePinHold(static_cast<gpio_num_t>(Pins::kBoot))) return;
  for (uint8_t pin : kRadioPins) {
    if (!releasePinHold(static_cast<gpio_num_t>(pin))) return;
  }
  beginBoard();
  Serial.printf("wake cause=%d\n", esp_sleep_get_wakeup_cause());
  const int16_t radioState = boardRadio.begin();
  Serial.printf("radio=%d\n", radioState);
  if (radioState != RADIOLIB_ERR_NONE) {
    Serial.println("SLEEP aborted: LR1121 init failed; reset to retry");
    return;
  }
  const bool batteryReady = battery.begin();
  if (!batteryReady)
    Serial.printf("AXP2602 unavailable: %s; continue without gauge\n", battery.lastError());
  if (!display.begin()) {
    Serial.printf("SLEEP aborted: display init failed: %s\n", display.lastError());
    return;
  }
  char timerLine[32];
  snprintf(timerLine, sizeof(timerLine), "TIMER %lus / GPIO0", (unsigned long)kWakeupSeconds);
  const char *lines[] = {timerLine, "LR1121 POWER OFF",
                         batteryReady ? "AXP SLEEP" : "AXP NOT FOUND"};
  if (!display.showLines("SLEEP", lines, 3, true)) {
    Serial.printf("SLEEP aborted: display refresh failed: %s\n", display.lastError());
    return;
  }
  delay(1200);

  const gpio_num_t boot = static_cast<gpio_num_t>(Pins::kBoot);
  waitForBootRelease(false);
  if (!checkEsp(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL), "clear wake sources") ||
      !checkEsp(esp_sleep_enable_timer_wakeup(uint64_t(kWakeupSeconds) * 1000000ULL), "timer wake") ||
      !checkEsp(esp_sleep_enable_ext0_wakeup(boot, 0), "GPIO0 wake") ||
      !checkEsp(rtc_gpio_init(boot), "GPIO0 RTC init") ||
      !checkEsp(rtc_gpio_set_direction(boot, RTC_GPIO_MODE_INPUT_ONLY), "GPIO0 RTC input") ||
      !checkEsp(rtc_gpio_pullup_en(boot), "GPIO0 pull-up") ||
      !checkEsp(rtc_gpio_pulldown_dis(boot), "GPIO0 pull-down")) return;

  const int16_t sleepState = boardRadio.sleep();
  if (sleepState != RADIOLIB_ERR_NONE) {
    Serial.printf("SLEEP aborted: LR1121 sleep failed (%d)\n", sleepState);
    return;
  }
  Serial.println("LR1121 Sleep accepted before power off");
  if (batteryReady) {
    uint8_t mode = 0;
    if (!battery.sleep() || !i2cRead(AXP2602::kAddress, 0x02, &mode, 1) || !(mode & 0x01U)) {
      Serial.println("SLEEP aborted: AXP2602 sleep verification failed");
      return;
    }
    Serial.println("AXP2602 entered sleep mode");
  }
  // Keep power on until SPI has stopped and all radio signals are isolated.
  if (!display.sleep(false)) {
    Serial.println("SLEEP aborted: display sleep failed");
    return;
  }
  Wire.end();
  if (!isolateRadioPins() || !powerOffAndHold()) return;
  Serial.println("radio pins isolated; shared power held off");
  Serial.printf("start deep sleep: timer %lus / GPIO0 EXT0\n", (unsigned long)kWakeupSeconds);
  Serial.flush();
  waitForBootRelease(true);
  esp_deep_sleep_start();
}

void loop() {
  // On failure remain awake for diagnosis instead of entering incomplete sleep.
  delay(1000);
}
