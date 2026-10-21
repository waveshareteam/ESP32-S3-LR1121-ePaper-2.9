#include <FS.h>
#include <SD_MMC.h>
#include <cstring>
#include <WaveshareBoard.h>

using waveshare::Pins;
using waveshare::setSharedPower;

namespace {
constexpr char kTestDirectory[] = "/waveshare_sd_test";
constexpr char kTestFile[] = "/waveshare_sd_test/roundtrip.txt";
constexpr char kPayload[] =
    "Waveshare ESP32-S3 SDMMC round-trip test\n"
    "Only this file is created and removed by example 07.\n";

const char *cardTypeName(uint8_t type) {
  switch (type) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    default: return "UNKNOWN";
  }
}

bool listRootDirectory() {
  File root = SD_MMC.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("ERROR: cannot open root directory");
    return false;
  }

  Serial.println("Root directory (up to 16 entries):");
  unsigned int count = 0;
  File entry = root.openNextFile();
  while (entry && count < 16) {
    Serial.printf("  %s%s (%llu bytes)\n", entry.name(),
                  entry.isDirectory() ? "/" : "",
                  static_cast<unsigned long long>(entry.size()));
    entry.close();
    entry = root.openNextFile();
    ++count;
  }
  entry.close();
  root.close();
  if (count == 0) Serial.println("  <empty>");
  return true;
}

bool runRoundTripTest() {
  bool directoryCreated = false;
  bool fileCreated = false;
  bool passed = false;

  do {
    if (!SD_MMC.exists(kTestDirectory)) {
      if (!SD_MMC.mkdir(kTestDirectory)) {
        Serial.println("ERROR: cannot create test directory");
        break;
      }
      directoryCreated = true;
    } else {
      File directory = SD_MMC.open(kTestDirectory);
      const bool isDirectory = directory && directory.isDirectory();
      directory.close();
      if (!isDirectory) {
        Serial.println("ERROR: test path exists but is not a directory");
        break;
      }
    }

    if (SD_MMC.exists(kTestFile)) {
      Serial.printf("ERROR: refusing to overwrite %s; remove or rename it first\n",
                    kTestFile);
      break;
    }

    File output = SD_MMC.open(kTestFile, FILE_WRITE);
    if (!output) {
      Serial.println("ERROR: cannot create test file");
      break;
    }
    fileCreated = true;
    const size_t payloadBytes = sizeof(kPayload) - 1;
    const size_t written = output.write(
        reinterpret_cast<const uint8_t *>(kPayload), payloadBytes);
    output.flush();
    output.close();
    if (written != payloadBytes) {
      Serial.printf("ERROR: short write (%u/%u bytes)\n",
                    static_cast<unsigned int>(written),
                    static_cast<unsigned int>(payloadBytes));
      break;
    }

    File input = SD_MMC.open(kTestFile, FILE_READ);
    if (!input) {
      Serial.println("ERROR: cannot open test file for readback");
      break;
    }
    uint8_t readback[sizeof(kPayload)] = {};
    const size_t bytesRead = input.read(readback, payloadBytes);
    const bool hasTrailingData = input.available() != 0;
    input.close();
    if (bytesRead != payloadBytes || hasTrailingData ||
        std::memcmp(readback, kPayload, payloadBytes) != 0) {
      Serial.println("ERROR: readback verification failed");
      break;
    }

    Serial.printf("Write/read verification passed (%u bytes)\n",
                  static_cast<unsigned int>(payloadBytes));
    passed = true;
  } while (false);

  if (fileCreated && !SD_MMC.remove(kTestFile)) {
    Serial.println("ERROR: failed to remove test file");
    passed = false;
  }
  if (directoryCreated && !SD_MMC.rmdir(kTestDirectory)) {
    Serial.println("ERROR: failed to remove test directory");
    passed = false;
  }
  return passed;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nWaveshare SD card test");

  setSharedPower(true);
  if (!SD_MMC.setPins(Pins::kSdClk, Pins::kSdCmd, Pins::kSdD0)) {
    Serial.println("FINAL: FAIL (SDMMC pin setup)");
    setSharedPower(false);
    return;
  }

  Serial.printf("Mounting FAT card with SDMMC 1-bit: CMD=%u D0=%u CLK=%u\n",
                Pins::kSdCmd, Pins::kSdD0, Pins::kSdClk);
  if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 5)) {
    Serial.println("FINAL: FAIL (mount failed; card was not formatted)");
    setSharedPower(false);
    return;
  }

  const uint8_t type = SD_MMC.cardType();
  bool passed = type != CARD_NONE;
  if (passed) {
    Serial.printf("Card: %s, size=%llu MiB, total=%llu MiB, used=%llu MiB\n",
                  cardTypeName(type),
                  static_cast<unsigned long long>(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(SD_MMC.totalBytes() / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(SD_MMC.usedBytes() / (1024ULL * 1024ULL)));
    passed = listRootDirectory() && runRoundTripTest();
  } else {
    Serial.println("ERROR: no SD card detected");
  }

  SD_MMC.end();
  Serial.println("Filesystem synchronized and unmounted");
  setSharedPower(false);
  Serial.printf("FINAL: %s\n", passed ? "PASS" : "FAIL");
}

void loop() {
  delay(1000);
}
