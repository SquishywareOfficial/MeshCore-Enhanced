#pragma once
#if defined(XIAO_WIO_WIFI_TIME) && XIAO_WIO_WIFI_TIME
#include "WifiTime.h"
#include "ESP32Board.h"
class Esp32WifiTimeBackend : public wifi_time::Backend {
  ESP32Board& board;
  bool owned = false;
public:
  explicit Esp32WifiTimeBackend(ESP32Board& b) : board(b) {}
  bool available() override;
  bool start(const wifi_time::Settings&) override;
  bool connected() override;
  void startNtp() override;
  uint32_t receivedUtc() override;
  void stop() override;
  uint16_t batteryMilliVolts() override;
};
#endif
