#include "SelfTest.h"

#include <SD_MMC.h>
#include <esp_crc.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <cstring>
#include <time.h>

namespace {
uint8_t pattern(size_t index) {
  uint32_t x = index * 0x45D9F3BU;
  return (x ^ (x >> 13) ^ 0xA55A3CC3U) & 0xFF;
}

bool testMemory(size_t bytes, uint32_t caps, const char *name) {
  auto *data = static_cast<uint8_t *>(heap_caps_malloc(bytes, caps));
  if (!data) { Serial.printf("%s FAIL allocation\n", name); return false; }
  const int64_t started = esp_timer_get_time();
  for (size_t i = 0; i < bytes; ++i) data[i] = pattern(i);
  bool ok = true;
  for (size_t i = 0; i < bytes; ++i) {
    if (data[i] != pattern(i)) { ok = false; break; }
  }
  Serial.printf("%s %s bytes=%u CRC=%08lX us=%lld\n", name,
                ok ? "PASS" : "FAIL", unsigned(bytes),
                (unsigned long)esp_crc32_le(0, data, bytes), esp_timer_get_time() - started);
  free(data);
  return ok;
}

std::atomic<bool> timerFired{false};
void timerCallback(void *) { timerFired.store(true); }

bool validRtc(const waveshare::RtcTime &t) {
  if (t.voltageLow || t.year < 2000 || t.year > 2099 || t.month < 1 ||
      t.month > 12 || t.weekday > 6 || t.hour > 23 || t.minute > 59 || t.second > 59)
    return false;
  const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const unsigned limit = days[t.month - 1] + (t.month == 2 && t.year % 4 == 0);
  return t.day >= 1 && t.day <= limit;
}

time_t rtcSeconds(const waveshare::RtcTime &value) {
  struct tm fields = {};
  fields.tm_year = value.year - 1900;
  fields.tm_mon = value.month - 1;
  fields.tm_mday = value.day;
  fields.tm_hour = value.hour;
  fields.tm_min = value.minute;
  fields.tm_sec = value.second;
  fields.tm_isdst = -1;
  return mktime(&fields);
}

}

bool testCrypto() {
  static const uint8_t shaExpected[32] = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
  static const uint8_t key[16] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
  static const uint8_t plain[16] = {
    0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34};
  static const uint8_t aesExpected[16] = {
    0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32};
  uint8_t hash[32], cipher[16];
  const bool sha = mbedtls_sha256(reinterpret_cast<const uint8_t *>("abc"), 3, hash, 0) == 0 &&
                   memcmp(hash, shaExpected, sizeof(hash)) == 0;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  const bool aes = mbedtls_aes_setkey_enc(&ctx, key, 128) == 0 &&
                   mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, plain, cipher) == 0 &&
                   memcmp(cipher, aesExpected, sizeof(cipher)) == 0;
  mbedtls_aes_free(&ctx);
  return aes && sha;
}

bool testSystem() {
  Serial.printf("S3 cores=%u CPU=%uMHz flash=%u PSRAM=%u heap=%u\n",
                ESP.getChipCores(), getCpuFrequencyMhz(), ESP.getFlashChipSize(),
                ESP.getPsramSize(), ESP.getFreeHeap());
  const bool ram = testMemory(64 * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, "RAM");
  const bool psram = testMemory(1024 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, "PSRAM");
  uint32_t flashSize = 0, crc = 0;
  auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  bool flash = buffer && esp_flash_get_size(nullptr, &flashSize) == ESP_OK && flashSize > 0;
  const int64_t start = esp_timer_get_time();
  for (uint32_t offset = 0; flash && offset < flashSize; offset += 16384) {
    const size_t count = min(uint32_t(16384), flashSize - offset);
    flash = esp_flash_read(nullptr, buffer, offset, count) == ESP_OK;
    if (flash) crc = esp_crc32_le(crc, buffer, count);
    delay(1);
  }
  free(buffer);
  Serial.printf("FLASH_READ %s bytes=%lu CRC=%08lX us=%lld\n",
                flash ? "PASS" : "FAIL", (unsigned long)flashSize,
                (unsigned long)crc, esp_timer_get_time() - start);
  uint8_t randomData[64];
  esp_fill_random(randomData, sizeof(randomData));
  bool rng = false;
  for (size_t i = 1; i < sizeof(randomData); ++i) rng |= randomData[i] != randomData[0];
  const bool crypto = testCrypto();
  esp_timer_handle_t timer = nullptr;
  esp_timer_create_args_t args = {};
  args.callback = timerCallback;
  args.name = "selftest";
  timerFired.store(false);
  bool timerOk = esp_timer_create(&args, &timer) == ESP_OK;
  if (timerOk) timerOk = esp_timer_start_once(timer, 10000) == ESP_OK;
  delay(30);
  timerOk = timerOk && timerFired.load();
  if (timer) { esp_timer_stop(timer); esp_timer_delete(timer); }
  Serial.printf("RNG=%s AES/SHA256=%s TIMER=%s\n", rng ? "PASS" : "FAIL",
                crypto ? "PASS" : "FAIL", timerOk ? "PASS" : "FAIL");
  return ram && psram && flash && rng && crypto && timerOk &&
         ESP.getChipCores() == 2 && getCpuFrequencyMhz() == 240;
}

const char *testStorage() {
  // Schematic: CLK=12, CMD=10, D0=11, 1-bit SDMMC, shared GPIO38 power.
  if (!SD_MMC.setPins(12, 10, 11) || !SD_MMC.begin("/sdcard", true, false)) {
    Serial.println("SD N/T: insert FAT card; no format/write attempted");
    return "N/T";
  }
  alignas(4) uint8_t sector[512];
  const uint32_t sectors = min(uint64_t(2048), SD_MMC.cardSize() / sizeof(sector));
  uint32_t crc = 0;
  bool ok = sectors > 0;
  const int64_t started = esp_timer_get_time();
  for (uint32_t i = 0; ok && i < sectors; ++i) {
    ok = SD_MMC.readRAW(sector, i);
    if (ok) crc = esp_crc32_le(crc, sector, sizeof(sector));
    if ((i % 32) == 0) delay(1);
  }
  Serial.printf("SD READ_ONLY %s bytes=%lu CRC=%08lX us=%lld\n",
                ok ? "PASS" : "FAIL", (unsigned long)(sectors * sizeof(sector)),
                (unsigned long)crc, esp_timer_get_time() - started);
  return ok ? "PASS" : "FAIL";
}

bool testRtc(waveshare::PCF85063A &rtc) {
  using waveshare::RtcTime;
  if (!rtc.begin()) return false;
  RtcTime now = {};
  if (!rtc.read(now)) return false;
  if (!validRtc(now)) {
    const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month[4] = {};
    unsigned year, day, hour, minute, second;
    if (sscanf(__DATE__, "%3s %u %u", month, &day, &year) != 3 ||
        sscanf(__TIME__, "%u:%u:%u", &hour, &minute, &second) != 3) return false;
    const char *found = strstr(months, month);
    if (!found) return false;
    now = {uint16_t(year), uint8_t((found - months) / 3 + 1), uint8_t(day),
           0, uint8_t(hour), uint8_t(minute), uint8_t(second), false};
    // Gregorian weekday, Sunday=0, matching the PCF85063A convention.
    const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    unsigned y = year - (now.month < 3);
    now.weekday = (y + y / 4 - y / 100 + y / 400 + offsets[now.month - 1] + day) % 7;
    if (!rtc.set(now)) return false;
    RtcTime check = {};
    if (!rtc.read(check) || !validRtc(check)) return false;
    const double elapsed = difftime(rtcSeconds(check), rtcSeconds(now));
    if (elapsed < 0 || elapsed > 2) return false;
    now = check;
    Serial.println("RTC initialized from firmware compile time");
  }
  Serial.printf("RTC %04u-%02u-%02u %02u:%02u:%02u IRQ=%d\n", now.year,
                now.month, now.day, now.hour, now.minute, now.second,
                digitalRead(waveshare::Pins::kRtcIrq));
  return true;
}
