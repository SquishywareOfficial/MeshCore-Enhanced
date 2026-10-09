#pragma once
#include "EnvironmentReading.h"
#include <stdio.h>
#include <string.h>

// Templates let native tests use the real formatting/dispatch with a fake
// manager without linking unrelated radio and filesystem dependencies.
template<class Manager>
bool handleEnvironmentGet(Manager& manager, const char* config, char* reply) {
  if (strcmp(config, "environment") && strcmp(config, "temperature") &&
      strcmp(config, "humidity")) return false;
  const auto reading = manager.readEnvironment();
  if (reading.status != EnvironmentStatus::Success) {
    const char* error = reading.status == EnvironmentStatus::Unsupported
      ? "Error: unsupported" : reading.status == EnvironmentStatus::NotDetected
      ? "Error: environment sensor not detected" : reading.status == EnvironmentStatus::Disabled
      ? "Error: environment sensor disabled" : reading.status == EnvironmentStatus::WarmingUp
      ? "Error: environment sensor warming up" : "Error: environment sensor read failed";
    snprintf(reply, 160, "%s", error);
  } else if (!strcmp(config, "environment")) {
    if (reading.gpio >= 0) {
      snprintf(reply, 160, "> %s GPIO%d temperature=%.1f C humidity=%.1f %%RH",
               reading.sensor, reading.gpio, double(reading.temperature), double(reading.humidity));
    } else {
      snprintf(reply, 160, "> %s temperature=%.1f C humidity=%.1f %%RH",
               reading.sensor, double(reading.temperature), double(reading.humidity));
    }
  } else if (!strcmp(config, "temperature")) {
    snprintf(reply, 160, "> %.1f C", double(reading.temperature));
  } else {
    snprintf(reply, 160, "> %.1f %%RH", double(reading.humidity));
  }
  return true;
}
