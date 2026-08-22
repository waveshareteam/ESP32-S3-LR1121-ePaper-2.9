#include <Preferences.h>

#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WaveshareLoRaWAN.h>
#include <WaveshareRadio.h>

#include "multicast_credentials.h"

using namespace waveshare;

const LoRaWANBand_t Region = EU868;
LR1121BoardRadio boardRadio;
LoRaWANNode node(&boardRadio.radio(), &Region, 0);
LoRaWANStore stateStore;
Preferences multicastStore;
Epaper display;
uint32_t lastMulticastCounter = 0;
bool hasMulticastCounter = false;

[[noreturn]] void provisioningError() {
  Serial.println("PROVISIONING ERROR: replace unicast and multicast placeholders; radio is disabled");
  const char *lines[] = {"REPLACE OTAA VALUES", "REPLACE MC ADDRESS",
                         "REPLACE MC KEYS", "NO RADIO TRAFFIC"};
  display.showLines("CONFIG ERROR", lines, 4, true);
  while (true) delay(1000);
}

[[noreturn]] void stateError(const char *detail) {
  Serial.printf("STATE ERROR: %s; radio is disabled\n", detail);
  boardRadio.sleep();
  const char *lines[] = {detail, "RADIO DISABLED", "RESET AFTER REPAIR"};
  display.showLines("STATE ERROR", lines, 3, true);
  while (true) delay(1000);
}
void setup() {
  Serial.begin(115200);
  beginBoard();
  display.begin();
  if (!validEui(LORAWAN_JOIN_EUI) || !validEui(LORAWAN_DEV_EUI) ||
      !validKey(LORAWAN_NWK_KEY) || !validKey(LORAWAN_APP_KEY) ||
      MULTICAST_ADDRESS == 0 || !validKey(MULTICAST_APP_S_KEY) ||
      !validKey(MULTICAST_NWK_S_KEY)) provisioningError();
  int16_t state = boardRadio.beginLoRaWAN(868.1f);
  if (state != RADIOLIB_ERR_NONE) return;
  state = node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI,
                         LORAWAN_NWK_KEY, LORAWAN_APP_KEY);
  if (state != RADIOLIB_ERR_NONE || !stateStore.begin("lorawan12")) return;
  stateStore.restore(node);
  state = node.activateOTAA();
  if (!stateStore.saveNonces(node)) stateError("NVS NONCE WRITE");
  if (!node.isActivated()) return;
  if (!stateStore.saveSession(node)) stateError("NVS SESSION WRITE");
  if (!multicastStore.begin("multicast12", false))
    stateError("NVS MULTICAST OPEN");
  hasMulticastCounter = multicastStore.isKey("mc_fcnt");
  lastMulticastCounter = multicastStore.getUInt("mc_fcnt", 0);
  if (hasMulticastCounter && lastMulticastCounter == UINT32_MAX)
    stateError("MC FCNT EXHAUSTED");
  const uint32_t minimum = hasMulticastCounter ? lastMulticastCounter + 1U : 0U;
  state = node.startMulticastSession(RADIOLIB_LORAWAN_CLASS_C,
      MULTICAST_ADDRESS, MULTICAST_APP_S_KEY, MULTICAST_NWK_S_KEY, minimum);
  if (state != RADIOLIB_ERR_NONE) stateError("MULTICAST START FAILED");
  Serial.printf("multicast ready: state=%d address=%08lX minimum_fcnt=%lu\n",
                state, static_cast<unsigned long>(MULTICAST_ADDRESS),
                static_cast<unsigned long>(minimum));
}

void loop() {
  if (!node.isActivated()) { delay(100); return; }
  uint8_t payload[128] = {};
  size_t length = sizeof(payload);
  LoRaWANEvent_t event = {};
  const int16_t state = node.getDownlinkClassC(payload, &length, &event);
  if (state > 0 && event.multicast) {
    if (hasMulticastCounter && event.fCnt <= lastMulticastCounter) {
      Serial.printf("multicast replay rejected: FCnt=%lu\n",
                    static_cast<unsigned long>(event.fCnt));
      return;
    }
    // Commit replay state before exposing the multicast payload.
    if (multicastStore.putUInt("mc_fcnt", event.fCnt) != sizeof(uint32_t))
      stateError("MC FCNT WRITE FAILED");
    lastMulticastCounter = event.fCnt;
    hasMulticastCounter = true;
    Serial.printf("multicast accepted: port=%u FCnt=%lu bytes=%u\n",
                  event.fPort, static_cast<unsigned long>(event.fCnt),
                  static_cast<unsigned>(length));
    char group[32], counter[32], sizeText[24];
    snprintf(group, sizeof(group), "GROUP %08lX",
             static_cast<unsigned long>(MULTICAST_ADDRESS));
    snprintf(counter, sizeof(counter), "MC FCNT %lu",
             static_cast<unsigned long>(lastMulticastCounter));
    snprintf(sizeText, sizeof(sizeText), "PORT %u BYTES %u", event.fPort,
             static_cast<unsigned>(length));
    const char *lines[] = {"EU868 CLASS C", group, counter, sizeText};
    display.showLines("MULTICAST", lines, 4);
  }
  delay(20);
}
