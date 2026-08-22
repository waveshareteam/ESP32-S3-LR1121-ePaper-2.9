#include <WaveshareBoard.h>
#include <WaveshareEPaper.h>

using namespace waveshare;

Epaper display;
uint32_t refreshCount = 0;

void setup() {
  Serial.begin(115200);
  beginBoard();
  const PanelInfo &panel = selectedPanel();
  Serial.printf("e-paper: %s visible=%ux%u RAM=%ux%u BUSY=HIGH\n",
                panel.name, panel.visibleWidth, panel.height,
                panel.ramWidth, panel.height);
  if (!display.begin()) {
    Serial.printf("ERROR: %s\n", display.lastError());
    return;
  }
  const char *lines[] = {"LANDSCAPE MODE", "FIRST REFRESH FULL",
                         "THEN FAST x10"};
  if (!display.showLines("EPAPER", lines, 3, true))
    Serial.printf("ERROR: %s\n", display.lastError());
}

void loop() {
  if (++refreshCount % 5 != 0) {
    delay(1000);
    return;
  }
  char counter[32];
  snprintf(counter, sizeof(counter), "REFRESH %lu",
           static_cast<unsigned long>(refreshCount / 5));
  const char *lines[] = {selectedPanel().name, counter,
                         "FAST AUTO-LIMIT 10"};
  if (!display.showLines("EPAPER", lines, 3))
    Serial.printf("ERROR: %s\n", display.lastError());
}

