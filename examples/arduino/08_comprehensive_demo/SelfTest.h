#pragma once

#include <WavesharePeripherals.h>

bool testSystem();
// Returns PASS, FAIL or N/T (no mounted FAT card). Never formats or writes.
const char *testStorage();
bool testRtc(waveshare::PCF85063A &rtc);
bool testCrypto();
