#include <WaveshareBoard.h>

using namespace waveshare;

void setup() {
  Serial.begin(115200);
  beginBoard(false, false);
  Serial.println("WS2812: GPIO21, one GRB LED, brightness <= 48/255");
}

void loop() {
  static uint16_t hue = 0;
  const uint8_t phase = hue / 256;
  const uint8_t step = hue & 0xFF;
  const uint8_t a = static_cast<uint16_t>(step) * 48 / 255;
  const uint8_t b = 48 - a;
  if (phase == 0) setRgb(b, a, 0);
  else if (phase == 1) setRgb(0, b, a);
  else setRgb(a, 0, b);
  hue = (hue + 3) % 768;
  delay(15);
}

