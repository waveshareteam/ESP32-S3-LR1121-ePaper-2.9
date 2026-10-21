#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WaveshareLoRaWAN.h>
#include <WaveshareRadio.h>

#include "lorawan_credentials.h"

using namespace waveshare;

// Keep attributes on the first declaration; Arduino otherwise adds prototypes.
[[noreturn]] void provisioningError();
[[noreturn]] void storageError();

const LoRaWANBand_t Region = EU868;
LR1121BoardRadio boardRadio;
LoRaWANNode node(&boardRadio.radio(), &Region, 0);
LoRaWANStore stateStore;
Epaper display;
uint32_t lastUplink = 0;

[[noreturn]] void provisioningError() {
  Serial.println("PROVISIONING ERROR: replace OTAA placeholders; radio is disabled");
  const char *lines[] = {"REPLACE OTAA VALUES", "NO RADIO TRAFFIC"};
  display.showLines("CONFIG ERROR", lines, 2, true);
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
  if (!validEui(LORAWAN_JOIN_EUI) || !validEui(LORAWAN_DEV_EUI) ||
      !validKey(LORAWAN_NWK_KEY) || !validKey(LORAWAN_APP_KEY))
    provisioningError();
  int16_t state = boardRadio.beginLoRaWAN(868.1f);
  if (state != RADIOLIB_ERR_NONE) return;
  state = node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI,
                         LORAWAN_NWK_KEY, LORAWAN_APP_KEY);
  if (state != RADIOLIB_ERR_NONE || !stateStore.begin("lorawan11")) return;
  stateStore.restore(node);
  state = node.activateOTAA();
  if (!stateStore.saveNonces(node)) storageError();
  if (!node.isActivated()) {
    Serial.printf("OTAA ERROR: %d\n", state);
    return;
  }
  if (!stateStore.saveSession(node)) storageError();
  state = node.setClass(RADIOLIB_LORAWAN_CLASS_C);
  const uint32_t before = nextFCntUp(node);
  const char marker[] = "C";
  if (state == RADIOLIB_ERR_NONE)
    state = node.sendReceive(marker, 1, true);
  if (!stateStore.saveIfCounterAdvanced(node, before)) storageError();
  Serial.printf("Class C ready: state=%d\n", state);
}

void loop() {
  if (!node.isActivated()) { delay(100); return; }
  uint8_t payload[128] = {};
  size_t length = sizeof(payload);
  LoRaWANEvent_t event = {};
  int16_t state = node.getDownlinkClassC(payload, &length, &event);
  if (state > 0) {
    if (!stateStore.saveSession(node)) storageError();
    Serial.printf("Class C downlink: window=%d port=%u FCnt=%lu bytes=%u\n",
                  state, event.fPort, static_cast<unsigned long>(event.fCnt),
                  static_cast<unsigned>(length));
  }
  if (millis() - lastUplink >= 60000U) {
    const uint8_t up[] = {0x43, 0x4C, 0x41, 0x53, 0x53, 0x43};
    const uint32_t before = nextFCntUp(node);
    state = node.sendReceive(up, sizeof(up), 10, false);
    if (!stateStore.saveIfCounterAdvanced(node, before)) storageError();
    lastUplink = millis();
  }
  char line1[32], line2[32];
  snprintf(line1, sizeof(line1), "FCNT UP %lu",
           static_cast<unsigned long>(node.getFCntUp()));
  snprintf(line2, sizeof(line2), "LAST RX STATE %d", state);
  const char *lines[] = {"EU868 CLASS C", line1, line2,
                         "CONTINUOUS RX"};
  display.showLines("LORAWAN", lines, 4);
  delay(100);
}

