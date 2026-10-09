#pragma once
#include <Arduino.h>
#include <vector>
#define DHT11 11
#define INPUT 1
inline std::vector<int> dht_released_pins;
inline void pinMode(int pin, int) { dht_released_pins.push_back(pin); }

// Models the driver's same-packet cache; our manager must additionally avoid
// even calling it during warmup or a cached console/telemetry request.
class DHT {
  int pin;
  uint32_t last = 0;
  bool sampled = false;
  float t = NAN, h = NAN;
  void sample() {
    if (sampled && uint32_t(millis() - last) < 2000) return;
    last = millis(); sampled = true; ++reads;
    g_mock_millis += duration;
    t = failed ? NAN : temperature; h = failed ? NAN : humidity;
  }
public:
  inline static int begins = 0, reads = 0, last_pin = -1;
  inline static uint32_t duration = 25;
  inline static float temperature = 24, humidity = 55;
  inline static bool failed = false;
  DHT(int p, int) : pin(p) {}
  void begin() { ++begins; last_pin = pin; sampled = false; }
  float readTemperature() { sample(); return t; }
  float readHumidity() { sample(); return h; }
};
