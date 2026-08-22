#include "WaveshareEPaper.h"

#include <cstring>

namespace waveshare {

namespace {
SPISettings kEpaperSpi(10000000, MSBFIRST, SPI_MODE0);

// Compact 5x7 glyphs for ASCII 32..126, stored column first.
const uint8_t kFont[][5] PROGMEM = {
{0,0,0,0,0},{0,0,95,0,0},{0,7,0,7,0},{20,127,20,127,20},{36,42,127,42,18},{35,19,8,100,98},{54,73,85,34,80},{0,5,3,0,0},{0,28,34,65,0},{0,65,34,28,0},{20,8,62,8,20},{8,8,62,8,8},{0,80,48,0,0},{8,8,8,8,8},{0,96,96,0,0},{32,16,8,4,2},{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},{0,54,54,0,0},{0,86,54,0,0},{8,20,34,65,0},{20,20,20,20,20},{0,65,34,20,8},{2,1,81,9,6},{50,73,121,65,62},{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67},{0,127,65,65,0},{2,4,8,16,32},{0,65,65,127,0},{4,2,1,2,4},{64,64,64,64,64},{0,1,2,4,0},{32,84,84,84,120},{127,72,68,68,56},{56,68,68,68,32},{56,68,68,72,127},{56,84,84,84,24},{8,126,9,1,2},{12,82,82,82,62},{127,8,4,4,120},{0,68,125,64,0},{32,64,68,61,0},{127,16,40,68,0},{0,65,127,64,0},{124,4,24,4,120},{124,8,4,4,120},{56,68,68,68,56},{124,20,20,20,8},{8,20,20,24,124},{124,8,4,4,8},{72,84,84,84,32},{4,63,68,64,32},{60,64,64,32,124},{28,32,64,32,28},{60,64,48,64,60},{68,40,16,40,68},{12,80,80,80,60},{68,100,84,76,68},{0,8,54,65,0},{0,0,127,0,0},{0,65,54,8,0},{2,1,2,4,2}};
}

size_t Epaper::bufferSize() const {
  const PanelInfo &p = selectedPanel();
  return static_cast<size_t>(p.ramWidth / 8U) * p.height;
}

uint16_t Epaper::width() const { return selectedPanel().height; }
uint16_t Epaper::height() const { return selectedPanel().visibleWidth; }

void Epaper::command(uint8_t value) {
  SPI.beginTransaction(kEpaperSpi);
  digitalWrite(Pins::kEpaperDc, LOW);
  digitalWrite(Pins::kEpaperCs, LOW);
  SPI.transfer(value);
  digitalWrite(Pins::kEpaperCs, HIGH);
  SPI.endTransaction();
}

void Epaper::data(uint8_t value) { data(&value, 1); }

void Epaper::data(const uint8_t *values, size_t length) {
  SPI.beginTransaction(kEpaperSpi);
  digitalWrite(Pins::kEpaperDc, HIGH);
  digitalWrite(Pins::kEpaperCs, LOW);
  SPI.writeBytes(values, length);
  digitalWrite(Pins::kEpaperCs, HIGH);
  SPI.endTransaction();
}

bool Epaper::waitReady(const char *phase) {
  const uint32_t start = millis();
  while (digitalRead(Pins::kEpaperBusy) == HIGH) {
    if (millis() - start >= kBusyTimeoutMs) {
      lastError_ = phase;
      return false;
    }
    delay(10);
  }
  return true;
}

void Epaper::reset() {
  digitalWrite(Pins::kEpaperReset, LOW);
  delay(10);
  digitalWrite(Pins::kEpaperReset, HIGH);
  delay(10);
}

void Epaper::setRamWindow() {
  const PanelInfo &p = selectedPanel();
  command(0x11); data(0x03);
  command(0x44); data(0x00); data((p.ramWidth / 8U) - 1U);
  command(0x45); data(0x00); data(0x00);
  data((p.height - 1U) & 0xFFU); data((p.height - 1U) >> 8U);
  command(0x4E); data(0x00);
  command(0x4F); data(0x00); data(0x00);
}

bool Epaper::initializeController() {
  const PanelInfo &p = selectedPanel();
  reset();
  if (!waitReady("BUSY timeout after hardware reset")) return false;
  command(0x12);
  if (!waitReady("BUSY timeout after software reset")) return false;
  command(0x01); data((p.height - 1U) & 0xFFU);
  data((p.height - 1U) >> 8U); data(0x00);
  setRamWindow();
  command(0x3C); data(0x05);
  command(0x21); data(0x00); data(0x80);
  command(0x18); data(0x80);
  return waitReady("BUSY timeout after controller setup");
}

bool Epaper::begin() {
  pinMode(Pins::kEpaperBusy, INPUT);
  pinMode(Pins::kEpaperReset, OUTPUT);
  pinMode(Pins::kEpaperDc, OUTPUT);
  pinMode(Pins::kEpaperCs, OUTPUT);
  digitalWrite(Pins::kEpaperCs, HIGH);
  initialized_ = initializeController();
  if (initialized_) {
    clear();
    lastError_ = "none";
  }
  return initialized_;
}

void Epaper::clear(bool white) {
  memset(buffer_, white ? 0xFF : 0x00, bufferSize());
}

void Epaper::drawPixel(int16_t x, int16_t y, bool black) {
  const PanelInfo &p = selectedPanel();
  if (x < 0 || y < 0 || x >= static_cast<int16_t>(width()) ||
      y >= static_cast<int16_t>(height())) return;
  const uint16_t nativeX = y;
  const uint16_t nativeY = p.height - 1U - x;
  const size_t index = nativeY * (p.ramWidth / 8U) + nativeX / 8U;
  const uint8_t mask = 0x80U >> (nativeX & 7U);
  if (black) buffer_[index] &= ~mask;
  else buffer_[index] |= mask;
}

void Epaper::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      bool black) {
  const int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  const int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;
  while (true) {
    drawPixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) break;
    const int16_t twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void Epaper::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black) {
  drawLine(x, y, x + w - 1, y, black);
  drawLine(x, y + h - 1, x + w - 1, y + h - 1, black);
  drawLine(x, y, x, y + h - 1, black);
  drawLine(x + w - 1, y, x + w - 1, y + h - 1, black);
}

void Epaper::drawChar(int16_t x, int16_t y, char value, uint8_t scale,
                      bool black) {
  if (value < 32 || value > 126) value = '?';
  for (uint8_t column = 0; column < 5; ++column) {
    const uint8_t bits = pgm_read_byte(&kFont[value - 32][column]);
    for (uint8_t row = 0; row < 7; ++row) {
      if ((bits & (1U << row)) == 0) continue;
      for (uint8_t dx = 0; dx < scale; ++dx)
        for (uint8_t dy = 0; dy < scale; ++dy)
          drawPixel(x + column * scale + dx, y + row * scale + dy, black);
    }
  }
}

void Epaper::drawText(int16_t x, int16_t y, const char *text, uint8_t scale,
                      bool black) {
  if (text == nullptr || scale == 0) return;
  while (*text != '\0') {
    drawChar(x, y, *text++, scale, black);
    x += 6 * scale;
  }
}

void Epaper::writeRam() {
  setRamWindow();
  command(0x24); data(buffer_, bufferSize());
  if (fastCount_ == 0) {
    setRamWindow();
    command(0x26); data(buffer_, bufferSize());
  }
}

bool Epaper::refresh(RefreshMode requested) {
  if (!initialized_) { lastError_ = "display not initialized"; return false; }
  RefreshMode mode = requested;
  if (fastCount_ == 0 || fastCount_ >= kFastRefreshLimit) mode = RefreshMode::Full;
  writeRam();
  if (mode == RefreshMode::Fast) {
    setRamWindow();
    command(0x18); data(0x80);
    command(0x22); data(0xB1); command(0x20);
    if (!waitReady("BUSY timeout loading fast temperature")) return false;
    command(0x1A); data(0x64); data(0x00);
    command(0x22); data(0x91); command(0x20);
    if (!waitReady("BUSY timeout setting fast temperature")) return false;
    command(0x22); data(0xC7); command(0x20);
  } else if (mode == RefreshMode::Partial) {
    command(0x3C); data(0x80);
    command(0x22); data(0xFF); command(0x20);
  } else {
    command(0x3C); data(0x05);
    command(0x22); data(0xF7); command(0x20);
  }
  if (!waitReady("BUSY timeout during display refresh")) return false;
  fastCount_ = mode == RefreshMode::Full ? 1 : fastCount_ + 1;
  lastError_ = "none";
  return true;
}

bool Epaper::showLines(const char *title, const char *const *lines, size_t count,
                       bool forceFull) {
  clear();
  drawRect(0, 0, width(), height());
  drawText(8, 8, title == nullptr ? "STATUS" : title, 2);
  int16_t y = 30;
  for (size_t i = 0; i < count && y + 7 < height(); ++i, y += 12)
    drawText(8, y, lines[i] == nullptr ? "" : lines[i]);
  return refresh(forceFull ? RefreshMode::Full : RefreshMode::Fast);
}

bool Epaper::sleep() {
  if (!initialized_) return true;
  command(0x10); data(0x01);
  delay(100);
  initialized_ = false;
  setSharedPower(false);
  return true;
}

}  // namespace waveshare
