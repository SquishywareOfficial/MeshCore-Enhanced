#if defined(XIAO_WIO_WIFI_TIME) && XIAO_WIO_WIFI_TIME
#include "ESP32WifiTime.h"
#include <WiFi.h>
#include <esp_sntp.h>
#include <atomic>

namespace { std::atomic<uint32_t> received_epoch{0}; }
// ESP-IDF explicitly supports overriding this weak hook. Capture a fresh NTP
// response only; update the selected RTC and history clock on the main loop.
// This feature is gated to roles that have no other SNTP client.
extern "C" void sntp_sync_time(struct timeval* tv) {
  if (tv && tv->tv_sec >= wifi_time::Service::MinimumUtc && uint64_t(tv->tv_sec) <= UINT32_MAX)
    received_epoch.store(uint32_t(tv->tv_sec));
}
bool Esp32WifiTimeBackend::available() {
  return !board.isOtaWifiActive() && (owned || WiFi.getMode() == WIFI_OFF);
}
bool Esp32WifiTimeBackend::start(const wifi_time::Settings& prefs) {
  if (!available()) return false;
  WiFi.persistent(false); // credentials stored only in /prefs.json
  owned = true; board.setTimeWifiActive(true);
  if (!WiFi.mode(WIFI_STA)) { stop(); return false; }
  WiFi.setSleep(false);
  WiFi.setHostname("MeshCore-Enhanced");
  WiFi.begin(prefs.ssid, prefs.password);
  return true;
}
bool Esp32WifiTimeBackend::connected() { return WiFi.status() == WL_CONNECTED; }
void Esp32WifiTimeBackend::startNtp() {
  received_epoch.store(0);
  // Initialize SNTP directly: no timezone changes or blocking getLocalTime().
  esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_init();
}
uint32_t Esp32WifiTimeBackend::receivedUtc() { return received_epoch.exchange(0); }
uint16_t Esp32WifiTimeBackend::batteryMilliVolts() {
  // Same calibrated full battery voltage used by telemetry, not divider ADC volts.
  return board.getBatteryConnected() == 1 ? board.getBattMilliVolts() : 0;
}
void Esp32WifiTimeBackend::stop() {
  if (!owned) return;
  esp_sntp_stop(); received_epoch.store(0);
  if (!board.isOtaWifiActive()) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
  }
  owned = false; board.setTimeWifiActive(false);
}
#endif
