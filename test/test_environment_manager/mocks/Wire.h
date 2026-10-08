#pragma once
#include <Arduino.h>
class TwoWire {
  int address = 0;
public:
  bool ack = true, stalled = false;
  uint16_t timeout = 50;
  void beginTransmission(int a) { address = a; }
  int endTransmission() { return address == 0x44 && ack ? 0 : 1; }
  uint16_t getTimeOut() { return timeout; }
  void setTimeOut(uint16_t ms) { timeout = ms; }
};
inline TwoWire Wire;
