#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>
#include <WaveshareRadio.h>

using namespace waveshare;

LR1121BoardRadio boardRadio;
Epaper display;
uint32_t sequence = 0;
uint32_t nextPing = 0;

void showStatus(const char *phase, int16_t state, const String &payload = "") {
  char code[24], counts[32];
  snprintf(code, sizeof(code), "STATE %d", state);
  snprintf(counts, sizeof(counts), "SEQ %lu RSSI %.1f",
           static_cast<unsigned long>(sequence), boardRadio.radio().getRSSI());
  const char *lines[] = {phase, "868MHz BW125 SF7 CR5", code, counts,
                         payload.c_str()};
  display.showLines("LR1121", lines, 5);
}

void setup() {
  Serial.begin(115200);
  beginBoard();
  display.begin();
  const int16_t state = boardRadio.begin();
  Serial.printf("LR1121 init: %d, TCXO=3.0V, SPI=8MHz\n", state);
  showStatus(state == RADIOLIB_ERR_NONE ? "READY" : "INIT ERROR", state);
  nextPing = millis() + 3000U + static_cast<uint32_t>(random(3000));
}

void loop() {
  String received;
  int16_t state = boardRadio.radio().receive(received, 0, 500);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("RX: %s RSSI=%.1f SNR=%.1f\n", received.c_str(),
                  boardRadio.radio().getRSSI(), boardRadio.radio().getSNR());
    if (received.startsWith("PING ")) {
      String reply = "PONG " + received.substring(5);
      state = boardRadio.radio().transmit(reply);
      Serial.printf("TX reply: %s state=%d\n", reply.c_str(), state);
    }
    showStatus("RX", state, received);
  }
  if (static_cast<int32_t>(millis() - nextPing) >= 0) {
    String ping = "PING " + String(++sequence);
    state = boardRadio.radio().transmit(ping);
    Serial.printf("TX: %s state=%d\n", ping.c_str(), state);
    showStatus("TX", state, ping);
    nextPing = millis() + 3000U + static_cast<uint32_t>(random(3000));
  }
}

