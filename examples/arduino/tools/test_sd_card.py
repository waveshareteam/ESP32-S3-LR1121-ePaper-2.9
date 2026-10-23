#!/usr/bin/env python3
"""Exercise example 07 with mock SD/display drivers, including power sequencing.

Runs the real sketch; does not simulate SD electrical behavior or panel timing.
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
#include <cstring>
#include <string>
#include <algorithm>
inline std::string fault, contents, finalScreen, logText;
inline bool powered=false, mounted=false, spiActive=false, panelStarted=false;
inline bool panelSlept=false, fileExists=false, directoryExists=false;
inline unsigned writes=0, refreshes=0, ends=0;
constexpr uint8_t CARD_NONE=0, CARD_MMC=1, CARD_SD=2, CARD_SDHC=3;
constexpr int SDMMC_FREQ_DEFAULT=20000, FILE_READ=0, FILE_WRITE=1;
inline void delay(unsigned) {}
struct SerialMock {
 void begin(int) {}
 void println(const char *s) { logText+=std::string(s)+"\n"; }
 template<class... A> void printf(const char *fmt,A... args) {
  char line[512]; snprintf(line,sizeof(line),fmt,args...); logText+=line;
 }
};
inline SerialMock Serial;
struct SpiMock {
 void end() { assert(!mounted && powered); assert(!panelStarted || panelSlept); spiActive=false; }
};
inline SpiMock SPI;
struct File {
 bool valid=false, directory=false;
 explicit operator bool() const { return valid; }
 bool isDirectory() const { return directory; }
 File openNextFile() { assert(mounted); return {}; }
 const char *name() const { return "test.txt"; }
 size_t size() const { return contents.size(); }
 void close() { valid=false; }
 void flush() { assert(mounted && powered); }
 size_t write(const uint8_t *p,size_t n) {
  assert(mounted && powered); ++writes;
  contents.assign(reinterpret_cast<const char*>(p),n);
  return fault=="write" ? n-1 : n;
 }
 size_t read(uint8_t *p,size_t n) {
  assert(mounted && powered); n=std::min(n,contents.size());
  memcpy(p,contents.data(),n); if(fault=="readback" && n) p[0]^=0xFF;
  return n;
 }
 int available() { return 0; }
};
struct SdMock {
 bool setPins(int,int,int) { assert(powered); return fault!="pins"; }
 bool begin(const char*,bool oneBit,bool format,int,int) {
  assert(powered && oneBit && !format); mounted=fault!="mount"; return mounted;
 }
 uint8_t cardType() { return fault=="no-card" ? CARD_NONE : CARD_SDHC; }
 uint64_t cardSize() { return 32768ULL*1024*1024; }
 uint64_t totalBytes() { return cardSize(); }
 uint64_t usedBytes() { return 1024; }
 bool exists(const char *p) {
  assert(mounted && powered); return std::string(p).find(".txt")!=std::string::npos ? fileExists : directoryExists;
 }
 bool mkdir(const char*) { assert(mounted && powered); directoryExists=true; return true; }
 File open(const char *p,int mode=FILE_READ) {
  assert(mounted && powered);
  if(std::string(p)=="/") return {fault!="root",true};
  if(std::string(p).find(".txt")==std::string::npos) return {directoryExists,true};
  if(mode==FILE_WRITE) { fileExists=true; return {true,false}; }
  return {fileExists,false};
 }
 bool remove(const char*) {
  assert(mounted && powered); if(fault=="remove") return false;
  fileExists=false; contents.clear(); return true;
 }
 bool rmdir(const char*) {
  assert(mounted && powered); if(fileExists) return false;
  directoryExists=false; return true;
 }
 void end() { assert(powered && mounted); mounted=false; ++ends; }
};
inline SdMock SD_MMC;
namespace waveshare {
struct Pins { static constexpr uint8_t kSdClk=12,kSdCmd=10,kSdD0=11; };
inline void setSharedPower(bool on) {
 if(!on) { assert(!mounted && !spiActive); assert(!panelStarted || panelSlept); }
 powered=on;
}
inline void beginBoard(bool i2c,bool spi) {
 assert(!i2c && spi); powered=true; spiActive=true;
}
struct Epaper {
 bool begin() { assert(powered && spiActive); panelStarted=fault!="display-init"; return panelStarted; }
 const char *lastError() { return "injected display failure"; }
 bool showLines(const char *title,const char *const*,size_t count,bool=false) {
  assert(powered && spiActive && panelStarted && !panelSlept && count==4);
  ++refreshes;
  if(fault=="display-refresh") return false;
  if(std::string(title)!="SD TEST") { assert(!mounted); finalScreen=title; }
  return true;
 }
 bool sleep(bool off) {
  assert(!off && !mounted && powered && spiActive); panelSlept=true; return true;
 }
};
}
'''
HARNESS = r'''
#include "mock.h"
#include SKETCH
int main(int argc,char **argv) {
 fault=argc>1 ? argv[1] : "";
 if(fault=="exists") { fileExists=directoryExists=true; contents="user data"; }
 setup();
 assert(!mounted && !powered && !spiActive);
 assert(!panelStarted || panelSlept);
 const bool displayFault=fault=="display-init" || fault=="display-refresh";
 const bool sdPass=fault.empty() || displayFault;
 assert(logText.find(sdPass ? "FINAL: PASS" : "FINAL: FAIL")!=std::string::npos);
 assert(logText.find(displayFault ? "DISPLAY: FAIL" : "DISPLAY: PASS")!=std::string::npos);
 if(!displayFault) assert(finalScreen==(sdPass ? "SD PASS" : "SD FAIL"));
 if(fault=="pins" || fault=="mount") assert(ends==0 && writes==0);
 else assert(ends==1);
 if(sdPass) assert(writes==1 && !fileExists && !directoryExists);
 if(fault=="exists") assert(writes==0 && contents=="user data" && fileExists && directoryExists);
 if(fault=="display-refresh") assert(refreshes==1); // No repeated failing refreshes.
}
'''


def main():
    cases = ('', 'pins', 'mount', 'no-card', 'root', 'exists', 'write',
             'readback', 'remove', 'display-init', 'display-refresh')
    with tempfile.TemporaryDirectory(prefix='sd-card-test-') as temp:
        work = Path(temp)
        (work / 'mock.h').write_text(MOCK)
        for name in ('FS.h', 'SD_MMC.h', 'WaveshareBoard.h', 'WaveshareEPaper.h'):
            (work / name).write_text('#include "mock.h"\n')
        (work / 'test.cpp').write_text(HARNESS)
        exe = work / 'test'
        subprocess.run([
            'g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', f'-I{work}',
            f'-DSKETCH="{ROOT / "07_sd_card/07_sd_card.ino"}"',
            str(work / 'test.cpp'), '-o', str(exe),
        ], check=True)
        for case in cases:
            subprocess.run([str(exe), case], check=True)
    print(f'PASS: {len(cases)} SD/display scenarios; unmount before sleep/power-off; existing file preserved')


if __name__ == '__main__':
    main()
