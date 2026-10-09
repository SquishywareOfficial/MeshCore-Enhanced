#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#if ENV_INCLUDE_DHT11
#include <DHT.h>
#endif

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;
#if ENV_INCLUDE_DHT11
  EnvironmentSensor environment_sensor = EnvironmentSensor::Auto;
  int environment_gpio = 2;
  DHT dht{2, DHT11};
  uint32_t dht_last_attempt = 0;
  EnvironmentReading dht_reading;
  EnvironmentReading readDht11();
#endif

  bool     gps_detected = false;
  bool     gps_active = false;
  uint32_t gps_update_interval_sec = 1;

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  EnvironmentReading readEnvironment() override;
#if ENV_INCLUDE_DHT11
  int getEnvironmentSensor() const override { return static_cast<int>(environment_sensor); }
  int getEnvironmentGpio() const override { return environment_gpio; }
  bool configureEnvironment(EnvironmentSensor sensor, int gpio) override;
#endif
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  #if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  void loop() override;
  #endif
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
