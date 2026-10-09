#pragma once
#include "EnvironmentReading.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

inline const char* environmentSensorName(int type) {
  switch (static_cast<EnvironmentSensor>(type)) {
    case EnvironmentSensor::Auto: return "auto";
    case EnvironmentSensor::None: return "none";
    case EnvironmentSensor::Sht4x: return "sht4x";
    case EnvironmentSensor::Dht11: return "dht11";
  }
  return "none";
}

// Board ownership is resolved before the driver can touch a GPIO. Preferences
// are saved only after successful validation and immediate application.
template<class Board, class Manager, class Prefs, class Save>
bool handleEnvironmentConfigCommand(Board& board, Manager& manager, Prefs& prefs,
                                    const char* command, char* reply, Save save) {
  const bool get_sensor = !strcmp(command, "get environment.sensor");
  const bool get_gpio = !strcmp(command, "get environment.gpio");
  auto argument = [command](const char* name) -> const char* {
    const size_t n = strlen(name);
    if (strncmp(command, name, n) || (command[n] && command[n] != ' ')) return nullptr;
    return command + n + (command[n] == ' ' ? 1 : 0);
  };
  const char* sensor_value = argument("set environment.sensor");
  const char* gpio_value = argument("set environment.gpio");
  if (!get_sensor && !get_gpio && !sensor_value && !gpio_value) return false;
  auto message = [reply](const char* text) { snprintf(reply, 160, "%s", text); };
  const int current = manager.getEnvironmentSensor();
  if (current < 0) { message("Error: unsupported"); return true; }
  if (get_sensor) {
    snprintf(reply, 160, "> %s", environmentSensorName(current));
  } else if (get_gpio) {
    snprintf(reply, 160, "> %d", manager.getEnvironmentGpio());
  } else {
    int type = current;
    int pin = manager.getEnvironmentGpio();
    if (sensor_value) {
      type = -1;
      for (int i = 0; i <= 3; ++i) if (!strcmp(sensor_value, environmentSensorName(i))) type = i;
      if (type < 0) { message("Error: expected auto, none, sht4x or dht11"); return true; }
    } else {
      if (current == int(EnvironmentSensor::Dht11)) {
        message("Error: set environment.sensor none before changing GPIO"); return true;
      }
      if (!strcmp(gpio_value, "default")) pin = 2;
      else {
        char* end = nullptr;
        errno = 0;
        const long parsed = strtol(gpio_value, &end, 10);
        if (*gpio_value < '0' || *gpio_value > '9' || *end || errno == ERANGE || parsed > 127) {
          message("Error: invalid environment GPIO"); return true;
        }
        pin = int(parsed);
      }
      // Validate even a disabled selection, without touching the pin electrically.
      if (!board.setEnvironmentGpio(pin)) { message("Error: GPIO unavailable"); return true; }
      board.setEnvironmentGpio(-1);
    }
    const int claim = type == int(EnvironmentSensor::Dht11) ? pin : -1;
    if (!board.setEnvironmentGpio(claim)) { message("Error: GPIO unavailable"); return true; }
    if (!manager.configureEnvironment(static_cast<EnvironmentSensor>(type), pin)) {
      board.setEnvironmentGpio(current == int(EnvironmentSensor::Dht11) ? manager.getEnvironmentGpio() : -1);
      message("Error: unsupported"); return true;
    }
    prefs.environment_sensor = uint8_t(type);
    prefs.environment_gpio = int8_t(pin);
    save();
    message("OK");
  }
  return true;
}

// Called after battery selection and before output assignments. Invalid saved
// settings disable the sensor and never probe a conflicting or unsafe pin.
template<class Board, class Manager, class Prefs>
void restoreEnvironmentSettings(Board& board, Manager& manager, Prefs& prefs) {
  if (manager.getEnvironmentSensor() < 0) return;
  int type = prefs.environment_sensor;
  int pin = prefs.environment_gpio;
  if (type > int(EnvironmentSensor::Dht11) || pin < 0 || pin > 127) {
    type = int(EnvironmentSensor::None); pin = 2;
  }
  if (type == int(EnvironmentSensor::Dht11) && !board.setEnvironmentGpio(pin))
    type = int(EnvironmentSensor::None);
  if (type != int(EnvironmentSensor::Dht11)) board.setEnvironmentGpio(-1);
  manager.configureEnvironment(static_cast<EnvironmentSensor>(type), pin);
  prefs.environment_sensor = uint8_t(type);
  prefs.environment_gpio = int8_t(pin);
}
