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
uint32_t sequence = 0;

[[noreturn]] void provisioningError() {
  Serial.println("PROVISIONING ERROR: replace OTAA placeholders; radio is disabled");
  const char *lines[] = {"REPLACE JOIN EUI", "REPLACE DEVICE EUI",
                         "REPLACE BOTH KEYS", "NO RADIO TRAFFIC"};
  display.showLines("CONFIG ERROR", lines, 4, true);
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
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("RADIO ERROR: %d\n", state);
    return;
  }
  state = node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI,
                         LORAWAN_NWK_KEY, LORAWAN_APP_KEY);
  if (state != RADIOLIB_ERR_NONE || !stateStore.begin("lorawan09")) {
    Serial.printf("LORAWAN INIT ERROR: %d\n", state);
    return;
  }
  const bool restored = stateStore.restore(node);
  Serial.printf("OTAA activation: saved_session=%s\n", restored ? "yes" : "no");
  state = node.activateOTAA();
  if (!stateStore.saveNonces(node)) storageError();
  if (!node.isActivated()) {
    Serial.printf("OTAA ERROR: %d\n", state);
    return;
  }
  if (!stateStore.saveSession(node)) storageError();
  Serial.printf("OTAA ready: state=%d FCntUp=%lu\n", state,
                static_cast<unsigned long>(node.getFCntUp()));
}

void loop() {
  if (!node.isActivated() || millis() - lastUplink < 60000U) {
    delay(50);
    return;
  }
  uint8_t uplink[4] = {0x57, 0x53,
                       static_cast<uint8_t>(sequence >> 8U),
                       static_cast<uint8_t>(sequence)};
  uint8_t downlink[64] = {};
  size_t downlinkLength = sizeof(downlink);
  LoRaWANEvent_t up = {}, down = {};
  const uint32_t before = nextFCntUp(node);
  const int16_t state = node.sendReceive(uplink, sizeof(uplink), 10,
                                         downlink, &downlinkLength, false,
                                         &up, &down);
  if (!stateStore.saveIfCounterAdvanced(node, before)) storageError();
  Serial.printf("uplink=%lu state=%d FCntUp=%lu downlink=%u bytes\n",
                static_cast<unsigned long>(sequence++), state,
                static_cast<unsigned long>(node.getFCntUp()),
                static_cast<unsigned>(downlinkLength));
  char count[32], result[32];
  snprintf(count, sizeof(count), "FCNT %lu SEQ %lu",
           static_cast<unsigned long>(node.getFCntUp()),
           static_cast<unsigned long>(sequence));
  snprintf(result, sizeof(result), "STATE %d DOWN %u", state,
           static_cast<unsigned>(downlinkLength));
  const char *lines[] = {"EU868 CLASS A", count, result, "NEXT 60 SECONDS"};
  display.showLines("LORAWAN OTAA", lines, 4);
  lastUplink = millis();
}

