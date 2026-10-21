#pragma once

#include <stdint.h>
#include "app_config.h"

struct DebouncedButton {
  bool raw = false, stable = false, armed = false;
  uint32_t changed = 0;
  bool poll(bool down, uint32_t now) {
    if (down != raw) { raw = down; changed = now; }
    if (now - changed < TEST_BUTTON_DEBOUNCE_MS) return false;
    if (!raw) armed = true;
    if (raw == stable) return false;
    stable = raw;
    return stable && armed;
  }
};

struct SleepGesture {
  bool pending = false;
  uint32_t firstAt = 0;
  bool update(bool pressed, uint32_t now) {
    if (pending && now - firstAt > TEST_GPIO0_DOUBLE_CLICK_MS) pending = false;
    if (!pressed) return false;
    if (pending) { pending = false; return true; }
    pending = true;
    firstAt = now;
    return false;
  }
};
