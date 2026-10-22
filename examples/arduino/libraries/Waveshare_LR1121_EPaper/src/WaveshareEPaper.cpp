#include "WaveshareEPaper.h"

#include <cstring>

#include "WaveshareFont.h"

namespace waveshare {

namespace {
SPISettings kEpaperSpi(10000000, MSBFIRST, SPI_MODE0);


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
  for (uint8_t row = 0; row < 14; ++row) {
    const uint8_t bits = pgm_read_byte(&kTerminusBold[(value - 32) * 14 + row]);
    for (uint8_t column = 0; column < 8; ++column) {
      if ((bits & (0x80U >> column)) == 0) continue;
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
    x += 8 * scale;
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

void Epaper::drawLabel(int16_t x, int16_t y, int16_t maxWidth,
                       const char *text, bool black) {
  if (!text || maxWidth < 8) return;
  const size_t capacity = maxWidth / 8;
  const size_t length = strlen(text);
  for (size_t i = 0; i < length && i < capacity; ++i) {
    const char ch = length > capacity && capacity >= 3 && i >= capacity - 3
                        ? '.' : text[i];
    drawChar(x + i * 8, y, ch, 1, black);
  }
}

bool Epaper::showLines(const char *title, const char *const *lines, size_t count,
                       bool forceFull) {
  if (count > 7 || (count && !lines)) {
    lastError_ = "showLines requires 0..7 lines";
    return false;
  }
  clear();
  const int16_t w = width(), h = height();
  const bool full = forceFull || fastCount_ == 0 || fastCount_ >= kFastRefreshLimit;
  for (int16_t y = 0; y < 22; ++y) drawLine(0, y, w - 1, y);
  drawLabel(7, 3, w - 72, title ? title : "STATUS", false);
  for (int16_t y = 3; y < 19; ++y) drawLine(w - 57, y, w - 8, y, false);
  drawText(w - 48, 4, full ? "FULL" : "FAST");
  drawRect(5, 25, w - 10, h - 30);
  const int16_t bodyHeight = h - 32;
  if (count == 7) {
    const int16_t split = w / 2;
    const int16_t footer = 26 + bodyHeight * 3 / 4;
    drawLine(split, 26, split, footer - 1);
    for (size_t i = 0; i < 6; ++i) {
      const int16_t top = 26 + bodyHeight * (i / 2) / 4;
      const int16_t bottom = 26 + bodyHeight * (i / 2 + 1) / 4;
      const int16_t left = (i % 2) ? split + 6 : 11;
      drawLabel(left, top + (bottom - top - 14) / 2,
                split - 17, lines[i]);
      if (i % 2 && i < 5) drawLine(10, bottom, w - 11, bottom);
    }
    for (int16_t y = footer; y < h - 6; ++y) drawLine(6, y, w - 7, y);
    drawLabel(9, footer + (h - 6 - footer - 14) / 2, w - 18, lines[6], false);
  } else {
    const size_t rows = count ? count : 1;
    for (size_t i = 0; i < rows; ++i) {
      const int16_t top = 26 + bodyHeight * i / rows;
      const int16_t bottom = 26 + bodyHeight * (i + 1) / rows;
      if (i) drawLine(10, top, w - 11, top);
      drawLabel(11, top + (bottom - top - 14) / 2, w - 22,
                count ? lines[i] : "No data");
    }
  }
  return refresh(forceFull ? RefreshMode::Full : RefreshMode::Fast);
}

bool Epaper::sleep(bool powerOff) {
  if (!initialized_) return true;
  command(0x10); data(0x01);
  delay(100);
  initialized_ = false;
  if (powerOff) setSharedPower(false);
  return true;
}

}  // namespace waveshare
