#pragma once
#include <stdint.h>
#include <vector>
#include <utility>
class CayenneLPP {
public:
  std::vector<std::pair<int,float>> temperatures, humidities;
  explicit CayenneLPP(int) {}
  void addTemperature(uint8_t ch, float v) { temperatures.emplace_back(ch,v); }
  void addRelativeHumidity(uint8_t ch, float v) { humidities.emplace_back(ch,v); }
  void addGPS(uint8_t,double,double,double) {}
};
