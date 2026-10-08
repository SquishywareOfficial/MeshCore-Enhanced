#pragma once
#include <stdint.h>
#include <math.h>

enum class EnvironmentStatus { Success, Unsupported, NotDetected, ReadFailed };
struct EnvironmentReading {
  EnvironmentStatus status = EnvironmentStatus::Unsupported;
  float temperature = 0;
  float humidity = 0;
};

// Shared console/telemetry sampling path. No cache, heaters or retries.
// The bus itself must enforce its transaction timeout; elapsed time is also
// checked so a late result is never presented as a valid fresh sample.
template<class Driver, class Clock>
EnvironmentReading sampleSht4x(Driver& driver, bool detected, Clock clock) {
  EnvironmentReading reading;
  reading.status = EnvironmentStatus::NotDetected;
  if (!detected) return reading;
  const uint32_t started = clock();
  float temperature = NAN, humidity = NAN;
  const int result = driver.measureHighPrecision(temperature, humidity);
  reading.status = EnvironmentStatus::ReadFailed;
  if (result != 0 || uint32_t(clock() - started) > 250 ||
      !isfinite(temperature) || !isfinite(humidity)) return reading;
  reading.status = EnvironmentStatus::Success;
  reading.temperature = temperature;
  reading.humidity = humidity;
  return reading;
}

template<class Bus>
void boundEnvironmentBusTimeout(Bus& bus) {
  const auto timeout = bus.getTimeOut();
  if (timeout == 0 || timeout > 50) bus.setTimeOut(50);
}
