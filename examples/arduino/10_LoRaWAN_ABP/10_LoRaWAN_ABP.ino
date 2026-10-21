#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WaveshareLoRaWAN.h>
#include <WaveshareRadio.h>

#include "abp_credentials.h"

using namespace waveshare;

// Keep attributes on the first declaration; Arduino otherwise adds prototypes.
[[noreturn]] void provisioningError();
[[noreturn]] void storageError();

const LoRaWANBand_t Region = EU868;
LR1121BoardRadio boardRadio;
LoRaWANNode node(&boardRadio.radio(), &Region, 0);
LoRaWANStore stateStore;
Epaper display;
uint32_t sequence = 0;
uint32_t lastUplink = 0;

[[noreturn]] void provisioningError() {
  Serial.println("PROVISIONING ERROR: replace ABP placeholders; radio is disabled");
  const char *lines[] = {"REPLACE DEVADDR", "REPLACE FOUR KEYS",
                         "NO RADIO TRAFFIC"};
  display.showLines("CONFIG ERROR", lines, 3, true);
  while (true) delay(1000);
}

[[noreturn]] void storageError() {
  Serial.println("NVS ERROR: session state is unsafe; radio is disabled");
  boardRadio.sleep();
  const char *lines[] = {"NVS WRITE FAILED", "RADIO DISABLED",
                         "RESET AFTER REPAIR"};
  display.showLines("STATE ERROR", lines, 3, true);
  while (true) delay(1000);
}
void setup() {
  Serial.begin(115200);
  beginBoard();
  display.begin();
  if (ABP_DEV_ADDR == 0 || !validKey(ABP_F_NWK_S_INT_KEY) ||
      !validKey(ABP_S_NWK_S_INT_KEY) || !validKey(ABP_NWK_S_ENC_KEY) ||
      !validKey(ABP_APP_S_KEY)) provisioningError();
  int16_t state = boardRadio.beginLoRaWAN(868.1f);
  if (state != RADIOLIB_ERR_NONE) return;
  state = node.beginABP(ABP_DEV_ADDR, ABP_F_NWK_S_INT_KEY,
                        ABP_S_NWK_S_INT_KEY, ABP_NWK_S_ENC_KEY,
                        ABP_APP_S_KEY);
  if (state != RADIOLIB_ERR_NONE || !stateStore.begin("lorawan10")) return;
  stateStore.restore(node);
  state = node.activateABP();
  Serial.printf("ABP activation: state=%d FCntUp=%lu\n", state,
                static_cast<unsigned long>(node.getFCntUp()));
  if (node.isActivated() && !stateStore.saveSession(node)) storageError();
}

void loop() {
  const bool button = digitalRead(Pins::kRadioButton) == LOW;
  if (!node.isActivated() || (!button && millis() - lastUplink < 60000U)) {
    delay(20);
    return;
  }
  const uint8_t payload[] = {0x41, 0x42, 0x50,
                             static_cast<uint8_t>(sequence++)};
  const uint32_t before = nextFCntUp(node);
  const int16_t state = node.sendReceive(payload, sizeof(payload), 10, false);
  if (!stateStore.saveIfCounterAdvanced(node, before)) storageError();
  Serial.printf("ABP uplink state=%d FCntUp=%lu\n", state,
                static_cast<unsigned long>(node.getFCntUp()));
  char counter[32], result[24];
  snprintf(counter, sizeof(counter), "FCNT %lu",
           static_cast<unsigned long>(node.getFCntUp()));
  snprintf(result, sizeof(result), "STATE %d", state);
  const char *lines[] = {"EU868 ABP", counter, result, "GPIO1 SEND"};
  display.showLines("LORAWAN ABP", lines, 4);
  lastUplink = millis();
  while (digitalRead(Pins::kRadioButton) == LOW) delay(10);
}

