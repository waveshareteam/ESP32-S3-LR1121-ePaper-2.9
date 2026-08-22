#include "WaveshareLoRaWAN.h"

namespace waveshare {

bool validEui(uint64_t value) {
  return value != 0 && value != UINT64_MAX;
}

bool validKey(const uint8_t *key, size_t length) {
  if (key == nullptr || length == 0) return false;
  bool allZero = true, allOnes = true;
  for (size_t i = 0; i < length; ++i) {
    allZero &= key[i] == 0x00;
    allOnes &= key[i] == 0xFF;
  }
  return !allZero && !allOnes;
}

uint32_t nextFCntUp(LoRaWANNode &node) {
  const uint8_t *session = node.getBufferSession();
  if (session == nullptr) return node.getFCntUp();
  const uint8_t *counter =
      &session[RADIOLIB_LORAWAN_SESSION_FCNT_UP];
  return static_cast<uint32_t>(counter[0]) |
         (static_cast<uint32_t>(counter[1]) << 8U) |
         (static_cast<uint32_t>(counter[2]) << 16U) |
         (static_cast<uint32_t>(counter[3]) << 24U);
}

bool LoRaWANStore::begin(const char *nameSpace) {
  open_ = nameSpace != nullptr && preferences_.begin(nameSpace, false);
  lastError_ = open_ ? "none" : "Preferences open failed";
  return open_;
}

bool LoRaWANStore::restore(LoRaWANNode &node) {
  if (!open_) return false;
  bool restored = false;
  if (preferences_.getBytesLength("nonces") == RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE] = {};
    preferences_.getBytes("nonces", buffer, sizeof(buffer));
    restored = node.setBufferNonces(buffer) == RADIOLIB_ERR_NONE;
  }
  if (restored &&
      preferences_.getBytesLength("session") == RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
    uint8_t buffer[RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {};
    preferences_.getBytes("session", buffer, sizeof(buffer));
    restored = node.setBufferSession(buffer) == RADIOLIB_ERR_NONE;
  }
  lastError_ = restored ? "none" : "no valid saved session";
  return restored;
}

bool LoRaWANStore::saveNonces(LoRaWANNode &node) {
  if (!open_ || node.getBufferNonces() == nullptr) return false;
  const size_t written = preferences_.putBytes(
      "nonces", node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  const bool ok = written == RADIOLIB_LORAWAN_NONCES_BUF_SIZE;
  lastError_ = ok ? "none" : "nonce save failed";
  return ok;
}

bool LoRaWANStore::saveSession(LoRaWANNode &node) {
  if (!open_ || node.getBufferSession() == nullptr) return false;
  if (!saveNonces(node)) return false;
  const size_t written = preferences_.putBytes(
      "session", node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  const bool ok = written == RADIOLIB_LORAWAN_SESSION_BUF_SIZE;
  lastError_ = ok ? "none" : "session save failed";
  return ok;
}

bool LoRaWANStore::saveIfCounterAdvanced(LoRaWANNode &node,
                                         uint32_t previousNextCounter) {
  return nextFCntUp(node) == previousNextCounter || saveSession(node);
}

void LoRaWANStore::end() {
  if (open_) preferences_.end();
  open_ = false;
}

}  // namespace waveshare
