#pragma once

#include "WaveshareBoard.h"

namespace waveshare {

enum class RefreshMode : uint8_t { Full, Fast, Partial };

class Epaper {
 public:
  static constexpr uint32_t kBusyTimeoutMs = 15000;
  static constexpr uint8_t kFastRefreshLimit = 10;

  bool begin();
  void clear(bool white = true);
  uint16_t width() const;
  uint16_t height() const;
  void drawPixel(int16_t x, int16_t y, bool black = true);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                bool black = true);
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                bool black = true);
  void drawText(int16_t x, int16_t y, const char *text, uint8_t scale = 1,
                bool black = true);
  bool refresh(RefreshMode requested = RefreshMode::Fast);
  bool showLines(const char *title, const char *const *lines, size_t count,
                 bool forceFull = false);
  bool sleep(bool powerOff = true);
  const char *lastError() const { return lastError_; }

 private:
  static constexpr size_t kMaxBufferBytes = 16U * 296U;
  uint8_t buffer_[kMaxBufferBytes];
  bool initialized_ = false;
  uint8_t fastCount_ = 0;
  const char *lastError_ = "not initialized";

  size_t bufferSize() const;
  void command(uint8_t value);
  void data(uint8_t value);
  void data(const uint8_t *values, size_t length);
  bool waitReady(const char *phase);
  void reset();
  bool initializeController();
  void setRamWindow();
  void writeRam();
  void drawLabel(int16_t x, int16_t y, int16_t maxWidth,
                 const char *text, bool black = true);
  void drawChar(int16_t x, int16_t y, char value, uint8_t scale, bool black);
};

}  // namespace waveshare

