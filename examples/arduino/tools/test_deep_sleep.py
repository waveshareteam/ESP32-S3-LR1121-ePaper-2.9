#!/usr/bin/env python3
"""Run the real sketch with a mock board to check power sequencing and failures.

This checks software ordering, retained GPIO release and wake configuration;
it does not measure current or simulate ESP32 pad electrical characteristics.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCK = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using esp_err_t = int;
using gpio_num_t = int;
constexpr int ESP_OK=0, ESP_ERR_NOT_SUPPORTED=2, RADIOLIB_ERR_NONE=0;
constexpr int GPIO_MODE_DISABLE=0, GPIO_MODE_OUTPUT=1;
constexpr int GPIO_PULLUP_DISABLE=0, GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
constexpr int ESP_SLEEP_WAKEUP_ALL=0, RTC_GPIO_MODE_INPUT_ONLY=0;
struct gpio_config_t { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; };
struct Pad { bool held=false; int mode=1; };
inline Pad pads[49];
inline bool power=false, spiStopped=false, panelAsleep=false, radioAsleep=false;
inline bool gaugeAsleep=false, deepHold=false, timer=false, ext0=false, rtc=false;
inline bool pullup=false, pulldown=true;
inline unsigned sleeps=0, digitalReads=0, rtcReads=0;
inline std::string fault;
inline std::vector<std::string> trace;
inline bool step(const std::string &s) { trace.push_back(s); return s != fault; }
inline int result(const std::string &s) { return step(s) ? ESP_OK : 1; }
inline const int radioPins[]={6,42,39,41,40,46,45};
struct FakeSerial {
 void begin(int) {} void println(const char*) {} void flush() {}
 template<class... A> void printf(const char*, A...) {}
};
inline FakeSerial Serial;
inline void delay(unsigned) {}
inline const char *esp_err_to_name(int) { return "mock failure"; }
inline void gpio_deep_sleep_hold_dis() { deepHold=false; }
inline void gpio_deep_sleep_hold_en() { deepHold=true; }
inline int gpio_hold_dis(int pin) {
 int r=result("release:"+std::to_string(pin)); if (!r) pads[pin].held=false; return r;
}
inline bool rtc_gpio_is_valid_gpio(int pin) { return pin<=21; }
inline int rtc_gpio_deinit(int pin) { return result("rtc-release:"+std::to_string(pin)); }
inline int gpio_config(const gpio_config_t *c) {
 int pin=__builtin_ctzll(c->pin_bit_mask);
 int r=result("config:"+std::to_string(pin)); if (r) return r;
 if (pin!=38) {
  assert(spiStopped && radioAsleep && panelAsleep && power);
  assert(c->mode==GPIO_MODE_DISABLE && !c->pull_up_en && !c->pull_down_en);
 }
 pads[pin].mode=c->mode; return ESP_OK;
}
inline int gpio_hold_en(int pin) {
 int r=result("hold:"+std::to_string(pin)); if (r) return r;
 if (pin==46) return ESP_ERR_NOT_SUPPORTED;
 pads[pin].held=true; return ESP_OK;
}
inline int gpio_set_level(int pin,int value) {
 assert(pin==38 && value==0 && spiStopped && panelAsleep && radioAsleep);
 for (int p:radioPins) assert(pads[p].mode==GPIO_MODE_DISABLE);
 int r=result("power-off"); if (!r) power=false; return r;
}
inline int button(unsigned &reads) {
 ++reads; // A low input and one bounce must not be accepted as released.
 return reads==1 || reads==3 ? 0 : 1;
}
inline int gpio_get_level(int pin) { assert(pin==0); return button(digitalReads); }
inline int rtc_gpio_get_level(int pin) { assert(pin==0 && rtc); return button(rtcReads); }
inline int esp_sleep_get_wakeup_cause() { return sleeps ? 2 : 0; }
inline int esp_sleep_disable_wakeup_source(int) { timer=ext0=false; return result("wake-clear"); }
inline int esp_sleep_enable_timer_wakeup(uint64_t us) {
 assert(us==600000000ULL); timer=true; return result("wake-timer");
}
inline int esp_sleep_enable_ext0_wakeup(int pin,int level) {
 assert(pin==0 && level==0 && digitalReads>=8); ext0=true; return result("wake-ext0");
}
inline int rtc_gpio_init(int) { rtc=true; return result("rtc-init"); }
inline int rtc_gpio_set_direction(int,int) { return result("rtc-input"); }
inline int rtc_gpio_pullup_en(int) { pullup=true; return result("rtc-pullup"); }
inline int rtc_gpio_pulldown_dis(int) { pulldown=false; return result("rtc-pulldown"); }
struct Slept {};
inline void esp_deep_sleep_start() {
 assert(!power && deepHold && pads[38].held && timer && ext0);
 assert(rtc && pullup && !pulldown && rtcReads>=8);
 assert(panelAsleep && radioAsleep && (gaugeAsleep || fault=="gauge-missing"));
 for (int pin:radioPins) assert(pads[pin].mode==GPIO_MODE_DISABLE && (pin==46 || pads[pin].held));
 ++sleeps; throw Slept{};
}
struct FakeSPI { void end() { assert(panelAsleep && radioAsleep); spiStopped=true; } };
struct FakeWire { void end() {} };
inline FakeSPI SPI;
inline FakeWire Wire;
namespace waveshare {
struct Pins {
 static constexpr uint8_t kBoot=0,kRadioDio1=6,kRadioCs=42,kRadioReset=39;
 static constexpr uint8_t kRadioBusy=41,kSpiSck=40,kSpiMiso=46,kSpiMosi=45,kSharedPower=38;
};
inline void beginBoard() {
 assert(!deepHold);
 for (int p:radioPins) assert(!pads[p].held);
 assert(!pads[38].held);
 power=true; spiStopped=panelAsleep=radioAsleep=gaugeAsleep=false;
 rtc=false; digitalReads=rtcReads=0;
 for (int p:radioPins) pads[p].mode=1;
}
struct Epaper {
 bool begin() { return step("panel-init"); }
 const char *lastError() { return "mock failure"; }
 bool showLines(const char*,const char*const*,unsigned,bool) { return step("panel-refresh"); }
 bool sleep(bool powerOff) {
  assert(!powerOff && power && radioAsleep);
  panelAsleep=step("panel-sleep"); return panelAsleep;
 }
};
struct AXP2602 {
 static constexpr uint8_t kAddress=0x62;
 bool begin() { return fault!="gauge-missing"; }
 const char *lastError() { return "mock missing gauge"; }
 bool sleep() { assert(power && radioAsleep); gaugeAsleep=step("gauge-sleep"); return gaugeAsleep; }
};
inline bool i2cRead(uint8_t addr,uint8_t reg,uint8_t *mode,unsigned size) {
 assert(addr==0x62 && reg==2 && size==1 && gaugeAsleep && power);
 *mode=fault=="gauge-not-asleep" ? 0 : 1; return step("gauge-read");
}
struct LR1121BoardRadio {
 int begin() { assert(power); return result("radio-init"); }
 int16_t sleep() { assert(power && !spiStopped); int r=result("radio-sleep"); radioAsleep=!r; return r; }
};
}
'''
HARNESS = r'''
#include "mock.h"
#include SKETCH
int main(int argc,char **argv) {
 fault=argc>1?argv[1]:"";
 const bool shouldSleep=fault.empty() || fault=="gauge-missing";
 for (int i=0;i<(shouldSleep?2:1);++i) {
  try { setup(); assert(!shouldSleep); }
  catch (const Slept&) { assert(shouldSleep); }
 }
 assert(sleeps==(shouldSleep?2U:0U));
 if (fault.empty()) for (const auto &s:trace) puts(s.c_str());
 else if (fault!="gauge-missing" && fault!="gauge-not-asleep") {
  bool reached=false; for (const auto &s:trace) reached|=s==fault; assert(reached);
 }
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='sleep-host-') as tmp:
        d = Path(tmp)
        (d / 'mock.h').write_text(MOCK)
        for name in ('driver/gpio.h', 'driver/rtc_io.h', 'esp_sleep.h',
                     'WaveshareBoard.h', 'WaveshareEPaper.h',
                     'WavesharePeripherals.h', 'WaveshareRadio.h'):
            p = d / name
            p.parent.mkdir(exist_ok=True)
            p.write_text('#include "mock.h"\n')
        (d / 'main.cpp').write_text(HARNESS)
        sketch = ROOT / '08_deep_sleep/08_deep_sleep.ino'
        exe = d / 'sleep-test'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        f'-DSKETCH="{sketch}"', '-I', str(d),
                        str(d / 'main.cpp'), '-o', str(exe)], check=True)
        success = subprocess.check_output([str(exe)], text=True)
        faults = sorted(set(success.splitlines()) | {'gauge-not-asleep', 'gauge-missing'})
        for fault in faults:
            subprocess.run([str(exe), fault], check=True)
        print(f'PASS: two sleep/wake cycles, button bounce, missing gauge and {len(faults)-1} injected failures')

if __name__ == '__main__':
    main()
