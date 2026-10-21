#include "Demo.h"
#include "SelfTest.h"
#include "ButtonInput.h"
#include "app_config.h"

#include <WaveshareEPaper.h>
#include <WaveshareRadio.h>
#include <BLEDevice.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_crc.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cstring>

using namespace waveshare;

namespace {
Epaper display;
AXP2602 battery;
PCF85063A rtc;
LR1121BoardRadio boardRadio;
BLEScan *bleScan = nullptr;
bool displayReady = false, radioReady = false, batteryReady = false, rtcReady = false;
bool apReady = false, bleReady = false, started = false;
std::atomic<bool> stopping{false}, sleepRequested{false}, transmitting{false}, receiving{false};
std::atomic<uint32_t> txCount{0}, txErrors{0}, rxCount{0}, rxErrors{0}, bleCount{0};
std::atomic<uint32_t> iterations[2], stressErrors[2], stressDigest[2];
std::atomic<uint32_t> stackFree[2];
QueueHandle_t txQueue = nullptr;
EventGroupHandle_t stopped = nullptr;
EventBits_t workers = 0;
constexpr EventBits_t kRadioStopped = 1, kButtonsStopped = 2, kCpu0Stopped = 4, kCpu1Stopped = 8;
uint32_t lastUi = 0, lastLog = 0, lastReconnect = 0, shownRx = 0, shownTx = 0, shownErrors = 0;

// All pins in the switched 3V3 domain are isolated only after their drivers stop.
const uint8_t isolatedPins[] = {
  Pins::kRadioCs, Pins::kRadioReset, Pins::kRadioBusy, Pins::kRadioDio1,
  Pins::kSpiSck, Pins::kSpiMosi, Pins::kSpiMiso,
  Pins::kEpaperCs, Pins::kEpaperDc, Pins::kEpaperReset, Pins::kEpaperBusy,
  Pins::kWs2812, 10, 11, 12
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice) override { bleCount.fetch_add(1); }
} scanCallbacks;

void showMessage(const char *title, const char *a, const char *b, bool full = true) {
  const char *lines[] = {a, b};
  if (displayReady && !display.showLines(title, lines, 2, full))
    Serial.printf("EPAPER FAIL: %s\n", display.lastError());
}

// Workers own no peripheral after setting their completion bit.
void finishWorker(EventBits_t bit) {
  xEventGroupSetBits(stopped, bit);
  vTaskDelete(nullptr);
}

void radioTask(void *) {
  auto &radio = boardRadio.radio();
  int16_t status = radio.startReceive();
  receiving.store(status == RADIOLIB_ERR_NONE);
  if (status != RADIOLIB_ERR_NONE) ++rxErrors;
  uint32_t retryAt = millis();
  uint32_t sequence = 0;
  while (!stopping.load()) {
    uint8_t request;
    if (xQueueReceive(txQueue, &request, 0) == pdTRUE) {
      if (sleepRequested.load()) continue;
      transmitting.store(true);
      receiving.store(false);
      char payload[112];
      int length = snprintf(payload, sizeof(payload), "id=%s seq=%lu ms=%lu",
                            TEST_DEVICE_ID, (unsigned long)++sequence, (unsigned long)millis());
      if (length < 0 || size_t(length) >= sizeof(payload) - 14) {
        ++txErrors;
      } else {
        const uint32_t crc = esp_crc32_le(0, reinterpret_cast<uint8_t *>(payload), length);
        snprintf(payload + length, sizeof(payload) - length, " crc=%08lX", (unsigned long)crc);
        radio.standby();
        status = radio.transmit(payload);
        if (status == RADIOLIB_ERR_NONE) ++txCount;
        else ++txErrors;
        Serial.printf("LORA TX state=%d %s\n", status, payload);
      }
      transmitting.store(false);
      status = radio.startReceive();
      receiving.store(status == RADIOLIB_ERR_NONE);
      if (status != RADIOLIB_ERR_NONE) ++rxErrors;
      retryAt = millis();
    }
    if (receiving.load() && digitalRead(Pins::kRadioDio1) == HIGH) {
      String payload;
      status = radio.readData(payload);
      if (status == RADIOLIB_ERR_NONE) {
        ++rxCount;
        Serial.printf("LORA RX %s RSSI=%.1f SNR=%.1f freq_error=NOT_AVAILABLE\n",
                      payload.c_str(), radio.getRSSI(), radio.getSNR());
      } else {
        ++rxErrors;
        Serial.printf("LORA RX error=%d\n", status);
      }
      status = radio.startReceive();
      receiving.store(status == RADIOLIB_ERR_NONE);
      if (status != RADIOLIB_ERR_NONE) ++rxErrors;
      retryAt = millis();
    }
    if (!receiving.load() && millis() - retryAt >= 1000) {
      status = radio.startReceive();
      receiving.store(status == RADIOLIB_ERR_NONE);
      if (status != RADIOLIB_ERR_NONE) ++rxErrors;
      retryAt = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  receiving.store(false);
  finishWorker(kRadioStopped);
}


void buttonTask(void *) {
  DebouncedButton boot, send;
  SleepGesture sleep;
  while (!stopping.load()) {
    const uint32_t now = millis();
    if (sleep.update(boot.poll(digitalRead(Pins::kBoot) == LOW, now), now))
      sleepRequested.store(true);
    if (send.poll(digitalRead(Pins::kRadioButton) == LOW, now) && !sleepRequested.load()) {
      const uint8_t request = 1;
      if (!radioReady || !txQueue || xQueueSend(txQueue, &request, 0) != pdTRUE) {
        ++txErrors;
        Serial.println("LORA TX request rejected: unavailable or queue full");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  finishWorker(kButtonsStopped);
}

uint32_t nextPattern(uint32_t x) {
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return x;
}

void stressTask(void *arg) {
  const size_t core = reinterpret_cast<uintptr_t>(arg);
  constexpr size_t ramWords = 2048, psramWords = 16384;
  auto *ram = static_cast<volatile uint32_t *>(heap_caps_malloc(ramWords * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  auto *psram = static_cast<volatile uint32_t *>(heap_caps_malloc(psramWords * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!ram || !psram) {
    ++stressErrors[core];
    Serial.printf("CPU%u stress allocation FAIL\n", unsigned(core));
  } else {
    uint32_t round = 0;
    while (!stopping.load()) {
      uint32_t seed = 0xA55A1234U ^ ++round;
      uint32_t value = seed;
      for (size_t i = 0; i < psramWords; ++i) {
        value = nextPattern(value);
        psram[i] = value;
        if (i < ramWords) ram[i] = value;
      }
      value = seed;
      for (size_t i = 0; i < psramWords; ++i) {
        value = nextPattern(value);
        if (psram[i] != value || (i < ramWords && ram[i] != value)) ++stressErrors[core];
      }
      volatile float sum = 0;
      for (unsigned i = 0; i < 4096; ++i) sum = sum + 0.25f;
      if (sum != 1024.0f) ++stressErrors[core];
      uint8_t hash[32];
      if (mbedtls_sha256(const_cast<const uint8_t *>(reinterpret_cast<volatile uint8_t *>(ram)),
                         ramWords * 4, hash, 0) != 0 || !testCrypto()) ++stressErrors[core];
      else memcpy(&value, hash, sizeof(value));
      stressDigest[core].store(value);
      ++iterations[core];
      stackFree[core].store(uxTaskGetStackHighWaterMark(nullptr));
      // Preserve both idle tasks and all watchdogs under sustained load.
      vTaskDelay(1);
    }
  }
  free(const_cast<uint32_t *>(ram));
  free(const_cast<uint32_t *>(psram));
  finishWorker(core ? kCpu1Stopped : kCpu0Stopped);
}

bool launch(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
            UBaseType_t priority, BaseType_t core, EventBits_t bit) {
  if (xTaskCreatePinnedToCore(fn, name, stack, arg, priority, nullptr, core) != pdPASS) {
    Serial.printf("TASK %s FAIL\n", name);
    return false;
  }
  workers |= bit;
  return true;
}

void startWireless() {
  const bool modeOk = WiFi.mode(WIFI_AP_STA);
  apReady = modeOk && WiFi.softAP(TEST_WIFI_AP_SSID, TEST_WIFI_AP_PASSWORD,
                                 TEST_WIFI_AP_CHANNEL, false, TEST_WIFI_AP_MAX_CONN);
  WiFi.setSleep(false);
  if (strlen(TEST_WIFI_STA_SSID)) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(TEST_WIFI_STA_SSID, TEST_WIFI_STA_PASSWORD);
  } else Serial.println("STA NOT_CONFIGURED");
  Serial.printf("SoftAP=%s IP=%s\n", apReady ? "PASS" : "FAIL", WiFi.softAPIP().toString().c_str());
  BLEDevice::init("LR1121-S3-TEST");
  bleScan = BLEDevice::getScan();
  if (bleScan) {
    bleScan->setActiveScan(false);
    bleScan->setInterval(100);
    bleScan->setWindow(80);
    // Duplicate callbacks count advertisements without retaining a device map.
    bleScan->setAdvertisedDeviceCallbacks(&scanCallbacks, true, false);
    bleReady = bleScan->start(0, static_cast<void (*)(BLEScanResults)>(nullptr), false);
  }
  Serial.printf("BLE passive scan=%s\n", bleReady ? "PASS" : "FAIL");
}

void showRuntime(bool full = false) {
  char text[7][48];
  const char *lines[7];
  const uint32_t tx = txCount.load(), rx = rxCount.load(), errors = txErrors.load();
  snprintf(text[0], sizeof(text[0]), "MODE %s", transmitting.load() ? "LORA TX" :
           (receiving.load() ? "LORA RX" : "RADIO ERR"));
  snprintf(text[1], sizeof(text[1]), "CPU 2C %uM", getCpuFrequencyMhz());
  snprintf(text[2], sizeof(text[2]), "AP:%u STA:%s", apReady ? WiFi.softAPgetStationNum() : 0,
           WiFi.status() == WL_CONNECTED ? "UP" : (strlen(TEST_WIFI_STA_SSID) ? "WAIT" : "N/C"));
  if (!apReady) snprintf(text[2], sizeof(text[2]), "AP:ERROR");
  snprintf(text[3], sizeof(text[3]), "BLE:%u N:%08lX", bleReady,
           (unsigned long)bleCount.load());
  if (display.width() < 296) {
    if (bleReady) snprintf(text[3], sizeof(text[3]), "BLE %08lX", (unsigned long)bleCount.load());
    else snprintf(text[3], sizeof(text[3]), "BLE ERROR");
  }
  snprintf(text[4], sizeof(text[4]), "KEY1 TX");
  snprintf(text[5], sizeof(text[5]), "%s", display.width() >= 296 ? "G0 2xSLP 1xWAK" : "G0 2xSL/1xWK");
  if (display.width() >= 296)
    snprintf(text[6], sizeof(text[6]), "LORA T%08lX F%08lX R%08lX",
             (unsigned long)tx, (unsigned long)errors, (unsigned long)rx);
  else
    snprintf(text[6], sizeof(text[6]), "T%08lX F%08lX R%08lX",
             (unsigned long)tx, (unsigned long)errors, (unsigned long)rx);
  for (size_t i = 0; i < 7; ++i) lines[i] = text[i];
  if (displayReady && !display.showLines("RUNTIME", lines, 7, full))
    Serial.printf("EPAPER FAIL: %s\n", display.lastError());
  shownRx = rx; shownTx = tx; shownErrors = errors;
  lastUi = millis();
}

bool waitReleased(bool rtcInput = false) {
  const uint32_t start = millis();
  uint32_t highSince = start;
  while (millis() - start < 3000) {
    const bool bootHigh = rtcInput ? rtc_gpio_get_level(GPIO_NUM_0) : digitalRead(Pins::kBoot);
    if (!bootHigh || digitalRead(Pins::kRadioButton) == LOW) highSince = millis();
    else if (millis() - highSince >= TEST_BUTTON_RELEASE_MS) return true;
    delay(5);
  }
  return false;
}

bool stopRtcIrq() {
  uint8_t mode = 0, ctrl2 = 0;
  // PCF85063A: timer mode 0x11 TE/TIE; Control_2 0x01 AF/TF/AIE.
  return i2cRead(0x51, 0x11, &mode, 1) && i2cWrite(0x51, 0x11, mode & ~0x06U) &&
         i2cRead(0x51, 0x01, &ctrl2, 1) && i2cWrite(0x51, 0x01, ctrl2 & ~0xC8U);
}

void restartAfterSleepFailure(const char *step) {
  Serial.printf("PREPARE_SLEEP FAIL: %s; restarting to rebuild all services\n", step);
  Serial.flush();
  delay(100);
  ESP.restart();
}

void enterSleep() {
  if (!waitReleased()) {
    Serial.println("SLEEP cancelled: release GPIO0/GPIO1");
    sleepRequested.store(false);
    return;
  }
  Serial.println("state=PREPARE_SLEEP");
  showMessage("SLEEP", "Preparing low power", "GPIO0: wake");
  stopping.store(true);
  const EventBits_t done = xEventGroupWaitBits(stopped, workers, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
  if ((done & workers) != workers) { restartAfterSleepFailure("worker timeout"); return; }
  SD_MMC.end();
  if (bleScan && bleReady && !bleScan->stop()) { restartAfterSleepFailure("BLE stop"); return; }
  BLEDevice::deinit(false);
  if (!WiFi.mode(WIFI_OFF)) { restartAfterSleepFailure("Wi-Fi stop"); return; }
  bool radioOff = true;
  if (radioReady) {
    radioOff = boardRadio.radio().standby() == RADIOLIB_ERR_NONE;
    radioOff = (boardRadio.radio().clearIrqFlags(RADIOLIB_LR11X0_IRQ_ALL) == RADIOLIB_ERR_NONE) && radioOff;
    radioOff = (boardRadio.sleep() == RADIOLIB_ERR_NONE) && radioOff;
  }
  const bool panelOff = !displayReady || display.sleep(false);
  setRgb(0, 0, 0);
  const bool rtcOff = stopRtcIrq();
  // AXP2602 W1C flags: low SOC, SOC update, over-temperature, watchdog.
  const bool batteryOff = i2cWrite(0x62, 0x21, 0) && i2cWrite(0x62, 0x20, 0) &&
                          i2cWrite(0x62, 0x20, 0x0F) && battery.sleep();
  if (!radioOff || !panelOff || !rtcOff || !batteryOff) {
    restartAfterSleepFailure("peripheral shutdown"); return;
  }
  Wire.end();
  SPI.end();
  // Isolate before cutting power, then hold every pin across deep sleep.
  for (uint8_t pin : isolatedPins) {
    gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_FLOATING);
    if (gpio_hold_en(gpio) != ESP_OK) { restartAfterSleepFailure("GPIO isolation"); return; }
  }
  setSharedPower(false);
  if (gpio_hold_en(static_cast<gpio_num_t>(Pins::kSharedPower)) != ESP_OK) {
    restartAfterSleepFailure("power hold"); return;
  }
  gpio_deep_sleep_hold_en();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  bool wakeOk = rtc_gpio_init(GPIO_NUM_0) == ESP_OK;
  wakeOk = (rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK) && wakeOk;
  wakeOk = (rtc_gpio_pullup_dis(GPIO_NUM_0) == ESP_OK) && wakeOk;
  wakeOk = (rtc_gpio_pulldown_dis(GPIO_NUM_0) == ESP_OK) && wakeOk;
  wakeOk = (esp_sleep_enable_ext1_wakeup_io(1ULL << Pins::kBoot, ESP_EXT1_WAKEUP_ANY_LOW) == ESP_OK) && wakeOk;
  wakeOk = (esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF) == ESP_OK) && wakeOk;
  Serial.println("state=LOW_POWER; GPIO0 EXT1 wake only");
  Serial.flush();
  if (!wakeOk || !waitReleased(true) || rtc_gpio_get_level(GPIO_NUM_0) == 0) {
    restartAfterSleepFailure("wake input"); return;
  }
  esp_deep_sleep_start();
}

void logStatus() {
  BatteryData bat = {};
  RtcTime now = {};
  const bool batOk = batteryReady && battery.read(bat);
  const bool timeOk = rtcReady && rtc.read(now);
  Serial.printf("RUNTIME TX=%lu FAIL=%lu RX=%lu RXERR=%lu BLE=%lu AP=%u STA=%s\n",
                (unsigned long)txCount.load(), (unsigned long)txErrors.load(),
                (unsigned long)rxCount.load(), (unsigned long)rxErrors.load(),
                (unsigned long)bleCount.load(), apReady ? WiFi.softAPgetStationNum() : 0,
                WiFi.status() == WL_CONNECTED ? "UP" : "DOWN");
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("STA IP=%s gateway=%s RSSI=%d\n", WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
  Serial.printf("BAT %s %umV %.1fmA %u%% %dC RTC %s %02u:%02u:%02u heap=%u\n",
                batOk ? "PASS" : "FAIL", bat.voltageMv, bat.currentMa, bat.stateOfCharge,
                bat.temperatureC, timeOk ? "PASS" : "FAIL", now.hour, now.minute, now.second,
                ESP.getFreeHeap());
  for (size_t core = 0; core < 2; ++core)
    Serial.printf("CPU%u iterations=%lu errors=%lu digest=%08lX stack=%lu\n", unsigned(core),
                  (unsigned long)iterations[core].load(), (unsigned long)stressErrors[core].load(),
                  (unsigned long)stressDigest[core].load(), (unsigned long)stackFree[core].load());
}
}

void demoBegin() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(static_cast<gpio_num_t>(Pins::kSharedPower));
  for (uint8_t pin : isolatedPins) gpio_hold_dis(static_cast<gpio_num_t>(pin));
  rtc_gpio_hold_dis(GPIO_NUM_0);
  rtc_gpio_deinit(GPIO_NUM_0);
  setCpuFrequencyMhz(240);
  beginBoard();
  pinMode(Pins::kRtcIrq, INPUT);
  pinMode(Pins::kAxpIrq, INPUT);
  pinMode(Pins::kChargeStatus, INPUT);
  Serial.printf("state=%s wake=%d\n", esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED ?
                "ACTIVE_SELF_TEST" : "WAKE_REINIT", esp_sleep_get_wakeup_cause());
  setRgb(48, 0, 0); delay(150);
  setRgb(0, 48, 0); delay(150);
  setRgb(0, 0, 48); delay(150);
  setRgb(48, 48, 48);
  displayReady = display.begin();
  showMessage("SELF TEST", "Testing hardware...", "Please wait");
  const bool systemOk = testSystem();
  const bool i2cOk = i2cProbe(0x51) && i2cProbe(0x62);
  batteryReady = battery.begin();
  BatteryData value = {};
  const bool batOk = batteryReady && battery.read(value);
  rtcReady = testRtc(rtc);
  const int16_t radioState = boardRadio.begin(TEST_LORA_FREQUENCY_MHZ, TEST_LORA_BANDWIDTH_KHZ,
      TEST_LORA_SPREADING_FACTOR, TEST_LORA_CODING_RATE, TEST_LORA_OUTPUT_POWER_DBM,
      TEST_LORA_PREAMBLE_LENGTH, TEST_LORA_SYNC_WORD);
  radioReady = radioState == RADIOLIB_ERR_NONE;
  const char *sd = testStorage();
  const char *names[] = {"S3 SELF", "EPAPER", "I2C", "BATTERY", "RTC", "LORA"};
  const bool results[] = {systemOk, displayReady, i2cOk, batOk, rtcReady, radioReady};
  char rows[7][48];
  const char *lines[7];
  unsigned pass = 0, fail = 0;
  for (size_t i = 0; i < 6; ++i) {
    snprintf(rows[i], sizeof(rows[i]), "%-7s %s", names[i], results[i] ? "PASS" : "FAIL");
    lines[i] = rows[i];
    if (results[i]) ++pass; else ++fail;
    Serial.println(rows[i]);
  }
  if (strcmp(sd, "PASS") == 0) ++pass;
  if (strcmp(sd, "FAIL") == 0) ++fail;
  snprintf(rows[6], sizeof(rows[6]), "SD:%s P%u F%u LED:N/T", sd, pass, fail);
  lines[6] = rows[6];
  Serial.println("LED/USB/RESET/Wi-Fi throughput/LoRa air link: manual verification required");
  if (displayReady) display.showLines("BOARD TEST", lines, 7, true);
  delay(TEST_EPAPER_SUMMARY_HOLD_MS);
  stopped = xEventGroupCreate();
  txQueue = xQueueCreate(8, sizeof(uint8_t));
  if (!stopped || !txQueue) {
    showMessage("START FAILED", "Task allocation failed", "Reset to retry");
    Serial.println("START FAIL: allocation");
    return;
  }
  startWireless();
  if (radioReady && !launch(radioTask, "lora", 6144, nullptr, 2, 1, kRadioStopped)) radioReady = false;
  if (!launch(buttonTask, "buttons", 3072, nullptr, 2, 1, kButtonsStopped)) {
    showMessage("START FAILED", "Buttons unavailable", "Reset to retry");
    stopping.store(true);
    return;
  }
  for (uintptr_t core = 0; core < 2; ++core) {
    if (!launch(stressTask, core ? "stress1" : "stress0", 6144,
                reinterpret_cast<void *>(core), 1, core, core ? kCpu1Stopped : kCpu0Stopped))
      ++stressErrors[core];
  }
  started = true;
  Serial.println("state=ACTIVE_STRESS; GPIO1 TX; GPIO0 double-click sleep");
  showRuntime(true);
}

void demoLoop() {
  if (!started) { delay(1000); return; }
  if (sleepRequested.load()) { enterSleep(); return; }
  const uint32_t now = millis();
  if (strlen(TEST_WIFI_STA_SSID) && WiFi.status() != WL_CONNECTED && now - lastReconnect >= 10000) {
    WiFi.reconnect();
    lastReconnect = now;
  }
  if (now - lastLog >= TEST_UART_STATUS_PERIOD_MS) { logStatus(); lastLog = millis(); }
  if (now - lastUi >= TEST_EPAPER_STATUS_PERIOD_MS ||
      ((rxCount.load() != shownRx || txCount.load() != shownTx || txErrors.load() != shownErrors) &&
       now - lastUi >= TEST_EPAPER_RX_MIN_INTERVAL_MS)) showRuntime();
  delay(10);
}
