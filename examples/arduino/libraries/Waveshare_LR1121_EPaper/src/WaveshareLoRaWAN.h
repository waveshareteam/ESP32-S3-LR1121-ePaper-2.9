#pragma once

#include <Preferences.h>
#include <RadioLib.h>

namespace waveshare {

bool validEui(uint64_t value);
bool validKey(const uint8_t *key, size_t length = 16);
uint32_t nextFCntUp(LoRaWANNode &node);

class LoRaWANStore {
 public:
  bool begin(const char *nameSpace);
  bool restore(LoRaWANNode &node);
  bool saveNonces(LoRaWANNode &node);
  bool saveSession(LoRaWANNode &node);
  bool saveIfCounterAdvanced(LoRaWANNode &node, uint32_t previousNextCounter);
  void end();
  const char *lastError() const { return lastError_; }

 private:
  Preferences preferences_;
  bool open_ = false;
  const char *lastError_ = "not initialized";
};

}  // namespace waveshare

