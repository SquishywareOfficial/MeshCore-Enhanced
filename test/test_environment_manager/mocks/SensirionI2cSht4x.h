#pragma once
#include <Wire.h>
class SensirionI2cSht4x {
  TwoWire* wire = nullptr;
public:
  inline static int init_error = 0, read_error = 0, reads = 0, probes = 0;
  inline static float temperature = -3.2f, humidity = 61.7f;
  void begin(TwoWire& w, int) { wire = &w; }
  int serialNumber(uint32_t& n) { ++probes; n = 123; return init_error; }
  int measureHighPrecision(float& t, float& h) {
    ++reads;
    // Driver's bounded send, conversion and receive, with a stuck bus.
    if (wire->stalled) { delay(wire->getTimeOut()); return 1; }
    delay(10); t = temperature; h = humidity; return read_error;
  }
};
