#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <cmath>

// Independent of both role-specific NodePrefs classes. Each feature checks its
// own board hook so multiplier-only boards keep their calibration support.
template<class Board, class Prefs, class Save>
bool handleBatteryCommand(Board& board, Prefs& prefs, const char* command,
                          char* reply, size_t capacity, Save save) {
  auto message = [reply, capacity](const char* text) {
    if (capacity) snprintf(reply, capacity, "%s", text);
  };
  auto argument = [command](const char* name) -> const char* {
    const size_t n = strlen(name);
    if (strncmp(command, name, n) || (command[n] && command[n] != ' ')) return nullptr;
    return command + n + (command[n] == ' ' ? 1 : 0);
  };
  if (!strcmp(command, "get battery.gpio")) {
    const int pin = board.getBatteryGpio();
    if (pin < 0) message("Error: unsupported");
    else if (capacity) snprintf(reply, capacity, "> %d", pin);
  } else if (!strcmp(command, "get battery.connected")) {
    const int connected = board.getBatteryConnected();
    message(connected < 0 ? "Error: unsupported" : connected ? "> on" : "> off");
  } else if (!strcmp(command, "get adc.multiplier")) {
    const float multiplier = board.getAdcMultiplier();
    if (multiplier == 0) message("Error: unsupported");
    else if (capacity) snprintf(reply, capacity, "> %.3f", double(multiplier));
  } else if (const char* value = argument("set battery.gpio")) {
    char* end = nullptr;
    long pin = -1;
    bool valid = !strcmp(value, "default");
    if (!valid) {
      errno = 0;
      pin = strtol(value, &end, 10);
      valid = value[0] >= '0' && value[0] <= '9' && *end == 0 &&
              errno != ERANGE && pin >= 0 && pin <= 127;
    }
    if (board.getBatteryGpio() < 0) message("Error: unsupported");
    else if (board.getBatteryConnected() > 0) message("Error: turn battery.connected off first");
    else if (!valid || !board.setBatteryGpio(static_cast<int>(pin))) message("Error: invalid battery GPIO");
    else {
      prefs.battery_gpio = static_cast<int8_t>(pin);
      save();
      message("OK");
    }
  } else if (const char* value = argument("set battery.connected")) {
    if (strcmp(value, "on") && strcmp(value, "off")) message("Error: expected on or off");
    else if (!board.setBatteryConnected(!strcmp(value, "on"))) message("Error: unsupported");
    else {
      prefs.battery_connected = !strcmp(value, "on");
      save();
      message("OK");
    }
  } else if (const char* value = argument("set adc.multiplier")) {
    char* end = nullptr;
    errno = 0;
    const float multiplier = strtof(value, &end);
    if (!*value || end == value || *end || errno == ERANGE ||
        !std::isfinite(multiplier) || multiplier < 0 || multiplier > 10) {
      message("Error: invalid multiplier");
    } else if (!board.setAdcMultiplier(multiplier)) {
      message("Error: unsupported");
    } else {
      prefs.adc_multiplier = multiplier;
      save();
      if (multiplier == 0) message("OK - using default board multiplier");
      else if (capacity) snprintf(reply, capacity, "OK - multiplier set to %.3f", double(multiplier));
    }
  } else return false;
  return true;
}

// Restore selection before enabling. Invalid saved pins reserve the default but
// leave sensing off. This helper deliberately needs no output-mask preference.
template<class Board, class Prefs>
void restoreBatterySettings(Board& board, const Prefs& prefs) {
  const bool valid = board.setBatteryGpio(prefs.battery_gpio);
  if (!valid) board.setBatteryGpio(-1);
  board.setBatteryConnected(valid && prefs.battery_connected == 1);
}
