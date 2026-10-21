#!/usr/bin/env python3
"""Run the actual e-paper renderer and button logic on the host; no board needed.

Optional --preview-dir writes pixel-exact PGM images outside the source tree.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / 'libraries/Waveshare_LR1121_EPaper/src'

ARDUINO = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#define PROGMEM
#define pgm_read_byte(p) (*(p))
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define MSBFIRST 1
#define SPI_MODE0 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return LOW; }
inline void delay(unsigned) {}
inline uint32_t millis() { return 0; }
'''
SPI = r'''
#pragma once
struct SPISettings { SPISettings(int, int, int) {} };
struct FakeSPI {
 void beginTransaction(SPISettings) {}
 void endTransaction() {}
 void transfer(unsigned char) {}
 void writeBytes(const unsigned char *, unsigned long) {}
};
extern FakeSPI SPI;
'''
HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#define private public
#include "WaveshareEPaper.h"
#undef private
#include "ButtonInput.h"
FakeSPI SPI;
namespace waveshare {
PanelInfo panel{EpaperModel::Epaper290,"test",128,128,296};
const PanelInfo &selectedPanel() { return panel; }
void setSharedPower(bool) {}
}
using namespace waveshare;
void save(const Epaper &e, const std::string &name) {
 FILE *f = fopen(name.c_str(), "wb"); assert(f);
 fprintf(f, "P5\n%u %u\n255\n", e.width(), e.height());
 for (int y=0; y<e.height(); ++y) for (int x=0; x<e.width(); ++x) {
   size_t i=(panel.height-1-x)*(panel.ramWidth/8)+y/8;
   fputc(e.buffer_[i] & (0x80 >> (y & 7)) ? 255 : 0, f);
 }
 fclose(f);
}
int main(int argc, char **argv) {
 DebouncedButton button;
 assert(!button.poll(false, 0));
 assert(!button.poll(false, 40));
 assert(!button.poll(true, 50));
 assert(!button.poll(false, 60));
 assert(!button.poll(true, 70));
 assert(!button.poll(true, 109));
 assert(button.poll(true, 110));
 assert(!button.poll(true, 900));
 assert(!button.poll(false, 910));
 assert(!button.poll(false, 950));
 assert(!button.poll(true, 960));
 assert(button.poll(true, 1000));
 DebouncedButton heldAtBoot;
 assert(!heldAtBoot.poll(true, 0));
 assert(!heldAtBoot.poll(true, 40));
 assert(!heldAtBoot.poll(true, 2000));
 SleepGesture gesture;
 assert(!gesture.update(true, 10));
 assert(!gesture.update(false, 700));
 assert(!gesture.update(true, 800));
 assert(gesture.update(true, 1000));
 assert(!gesture.update(true, UINT32_MAX-100));
 assert(gesture.update(true, 50));
 for (int width : {296,250}) {
  panel.height=width; panel.visibleWidth=width==296?128:122;
  Epaper e; assert(e.begin());
  const char *rows[]={"MODE LORA RX","CPU 2C 240M","AP:0 STA:N/C","BLE:1 N:01234567",
                      "KEY1 TX","G0 2xSLP 1xWAK","LORA T12345678 F00000000 R00000001"};
  assert(!e.showLines("BAD",nullptr,1));
  assert(!e.showLines("BAD",rows,8));
  for (size_t count=0; count<=7; ++count) assert(e.showLines("TEST",rows,count));
  // Clipped labels must not paint outside their assigned cell, even for long text.
  e.clear(); e.drawLabel(11,30,40,"ABCDEFGHIJK");
  for (int y=0; y<e.height(); ++y) for (int x=0; x<e.width(); ++x) {
   size_t i=(panel.height-1-x)*(panel.ramWidth/8)+y/8;
   bool ink=!(e.buffer_[i] & (0x80 >> (y&7)));
   if(ink) assert(x>=11 && x<51 && y>=30 && y<44);
  }
  const char *battery[]={"4120 mV  -123.5 mA","SOC 85%  TEMP 26 C","TTE 100  TTF 0 MIN"};
  assert(e.showLines("AXP2602",battery,3,true));
  if(argc>1) save(e,std::string(argv[1])+"/battery-"+std::to_string(width)+".pgm");
  if(width==250) {
    rows[3]="BLE 01234567"; rows[5]="G0 2xSL/1xWK";
    rows[6]="T12345678 F00000000 R00000001";
  }
  assert(e.showLines("RUNTIME",rows,7));
  if(argc>1) save(e,std::string(argv[1])+"/runtime-"+std::to_string(width)+".pgm");
 }
 puts("PASS: renderer bounds, 0..7 rows, both panel sizes, debounce and sleep gesture");
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview-dir', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='arduino-ui-test-') as temp:
        work = Path(temp)
        (work/'Arduino.h').write_text(ARDUINO)
        (work/'SPI.h').write_text(SPI)
        (work/'Wire.h').write_text('#pragma once\n')
        (work/'test.cpp').write_text(HARNESS)
        exe = work/'test'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer','-g',f'-I{work}',f'-I{LIB}',
                        f'-I{ROOT / "08_comprehensive_demo"}',str(work/'test.cpp'),
                        str(LIB/'WaveshareEPaper.cpp'),'-o',str(exe)], check=True)
        command = [str(exe)]
        if args.preview_dir:
            args.preview_dir.mkdir(parents=True,exist_ok=True)
            command.append(str(args.preview_dir.resolve()))
        subprocess.run(command,check=True)

if __name__ == '__main__':
    main()
