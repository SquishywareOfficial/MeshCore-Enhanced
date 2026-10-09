#include <gtest/gtest.h>
#include <Arduino.h>
#include <SensirionI2cSht4x.h>
#include <helpers/EnvironmentCLI.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
class EnvironmentManagerTest : public ::testing::Test {
protected:
  EnvironmentSensorManager manager;
  void SetUp() override {
    Wire = TwoWire{}; g_mock_millis = 0;
    SensirionI2cSht4x::init_error = SensirionI2cSht4x::read_error = 0;
    SensirionI2cSht4x::reads = SensirionI2cSht4x::probes = 0;
    SensirionI2cSht4x::temperature = -3.2f; SensirionI2cSht4x::humidity = 61.7f;
    DHT::begins = DHT::reads = 0; DHT::last_pin = -1;
    DHT::duration = 25; DHT::temperature = 24; DHT::humidity = 55; DHT::failed = false;
    dht_released_pins.clear();
  }
};
TEST_F(EnvironmentManagerTest, AckAloneDoesNotEstablishPresence) {
  SensirionI2cSht4x::init_error = 1;
  ASSERT_TRUE(manager.begin());
  EXPECT_EQ(EnvironmentStatus::NotDetected, manager.readEnvironment().status);
  EXPECT_EQ(1, SensirionI2cSht4x::probes); EXPECT_EQ(0, SensirionI2cSht4x::reads);
  CayenneLPP lpp(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
  EXPECT_TRUE(lpp.temperatures.empty()); EXPECT_TRUE(lpp.humidities.empty());
}
TEST_F(EnvironmentManagerTest, MissingAtBootNeedsRestart) {
  Wire.ack = false; ASSERT_TRUE(manager.begin()); Wire.ack = true;
  EXPECT_EQ(EnvironmentStatus::NotDetected, manager.readEnvironment().status);
  EXPECT_EQ(0, SensirionI2cSht4x::probes);
  ASSERT_TRUE(manager.begin());
  EXPECT_EQ(EnvironmentStatus::Success, manager.readEnvironment().status);
}
TEST_F(EnvironmentManagerTest, ConsoleAndTelemetryShareDriverAndChannel) {
  ASSERT_TRUE(manager.begin()); char reply[160];
  handleEnvironmentGet(manager, "environment", reply);
  EXPECT_STREQ("> SHT4x@0x44 temperature=-3.2 C humidity=61.7 %RH", reply);
  EXPECT_EQ(1, SensirionI2cSht4x::reads);
  CayenneLPP lpp(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
  ASSERT_EQ(1u, lpp.temperatures.size()); ASSERT_EQ(1u, lpp.humidities.size());
  EXPECT_EQ(2, lpp.temperatures[0].first); EXPECT_EQ(2, lpp.humidities[0].first);
  EXPECT_FLOAT_EQ(-3.2f, lpp.temperatures[0].second);
  EXPECT_FLOAT_EQ(61.7f, lpp.humidities[0].second); EXPECT_EQ(2, SensirionI2cSht4x::reads);
  CayenneLPP guest(100); manager.querySensors(0, guest);
  EXPECT_TRUE(guest.temperatures.empty()); EXPECT_EQ(2, SensirionI2cSht4x::reads);
  SensirionI2cSht4x::read_error = 1;
  CayenneLPP failure(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, failure);
  EXPECT_TRUE(failure.temperatures.empty()); EXPECT_TRUE(failure.humidities.empty());
  handleEnvironmentGet(manager, "temperature", reply);
  EXPECT_STREQ("Error: environment sensor read failed", reply);
}
TEST_F(EnvironmentManagerTest, FiniteTimeoutPreservesShorterBusLimit) {
  for (uint16_t timeout : {0, 15, 50, 1000}) {
    Wire.timeout = timeout; ASSERT_TRUE(manager.begin());
    EXPECT_EQ(timeout == 15 ? 15 : 50, Wire.timeout);
    Wire.timeout = 1000; // another bus client altered it after detection
    Wire.stalled = true; const auto started = millis(); char reply[160];
    handleEnvironmentGet(manager, "environment", reply);
    EXPECT_EQ(50, Wire.timeout);
    EXPECT_LE(millis() - started, 250u);
    EXPECT_STREQ("Error: environment sensor read failed", reply); Wire.stalled = false;
  }
}
TEST_F(EnvironmentManagerTest, NonFiniteValuesAddNoTelemetryFields) {
  ASSERT_TRUE(manager.begin());
  for (float bad : {NAN, INFINITY}) {
    SensirionI2cSht4x::temperature = bad;
    CayenneLPP lpp(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
    EXPECT_TRUE(lpp.temperatures.empty()); EXPECT_TRUE(lpp.humidities.empty());
  }
}
TEST_F(EnvironmentManagerTest, AutoAndNoneNeverTouchDhtPin) {
  ASSERT_TRUE(manager.begin());
  CayenneLPP automatic(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, automatic);
  EXPECT_EQ(0, DHT::begins); EXPECT_EQ(0, DHT::reads);
  ASSERT_TRUE(manager.configureEnvironment(EnvironmentSensor::None, 2));
  EXPECT_EQ(EnvironmentStatus::Disabled, manager.readEnvironment().status);
  CayenneLPP lpp(100); const int reads = SensirionI2cSht4x::reads;
  manager.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
  EXPECT_TRUE(lpp.temperatures.empty()); EXPECT_EQ(reads, SensirionI2cSht4x::reads);
  EXPECT_EQ(0, DHT::begins); EXPECT_TRUE(dht_released_pins.empty());
}
TEST_F(EnvironmentManagerTest, DhtWarmupAndPairSharedWithTelemetry) {
  ASSERT_TRUE(manager.begin());
  ASSERT_TRUE(manager.configureEnvironment(EnvironmentSensor::Dht11, 2));
  EXPECT_EQ(1, DHT::begins); EXPECT_EQ(2, DHT::last_pin);
  EXPECT_EQ(EnvironmentStatus::WarmingUp, manager.readEnvironment().status);
  g_mock_millis = 1999;
  EXPECT_EQ(EnvironmentStatus::WarmingUp, manager.readEnvironment().status);
  EXPECT_EQ(0, DHT::reads);
  g_mock_millis = 2000; char reply[160];
  handleEnvironmentGet(manager, "environment", reply);
  EXPECT_STREQ("> DHT11 GPIO2 temperature=24.0 C humidity=55.0 %RH", reply);
  EXPECT_EQ(1, DHT::reads);
  DHT::temperature = 30; DHT::humidity = 70;
  CayenneLPP lpp(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
  ASSERT_EQ(1u, lpp.temperatures.size()); ASSERT_EQ(1u, lpp.humidities.size());
  EXPECT_EQ(2, lpp.temperatures[0].first);
  EXPECT_FLOAT_EQ(24, lpp.temperatures[0].second); EXPECT_FLOAT_EQ(55, lpp.humidities[0].second);
  EXPECT_EQ(1, DHT::reads); EXPECT_EQ(0, SensirionI2cSht4x::reads);
  g_mock_millis = 4000;
  handleEnvironmentGet(manager, "temperature", reply); EXPECT_STREQ("> 30.0 C", reply);
  handleEnvironmentGet(manager, "humidity", reply); EXPECT_STREQ("> 70.0 %RH", reply);
  EXPECT_EQ(2, DHT::reads);
}
TEST_F(EnvironmentManagerTest, DhtFailuresNeverReturnOldSuccessAndAreRateLimited) {
  manager.begin(); manager.configureEnvironment(EnvironmentSensor::Dht11, 4);
  g_mock_millis = 2000; ASSERT_EQ(EnvironmentStatus::Success, manager.readEnvironment().status);
  DHT::failed = true; g_mock_millis = 4000;
  EXPECT_EQ(EnvironmentStatus::ReadFailed, manager.readEnvironment().status);
  CayenneLPP lpp(100); manager.querySensors(TELEM_PERM_ENVIRONMENT, lpp);
  EXPECT_TRUE(lpp.temperatures.empty()); EXPECT_TRUE(lpp.humidities.empty());
  EXPECT_EQ(2, DHT::reads);
  DHT::failed = false; g_mock_millis = 6000;
  EXPECT_EQ(EnvironmentStatus::Success, manager.readEnvironment().status);
  for (float invalid : {NAN, INFINITY, -1.0f, 101.0f}) {
    DHT::humidity = invalid; g_mock_millis += 2000;
    EXPECT_EQ(EnvironmentStatus::ReadFailed, manager.readEnvironment().status);
  }
  DHT::humidity = 55; DHT::duration = 251; g_mock_millis += 2000;
  EXPECT_EQ(EnvironmentStatus::ReadFailed, manager.readEnvironment().status);
}
TEST_F(EnvironmentManagerTest, DhtPermissionsDisableAndClockWrap) {
  manager.begin(); g_mock_millis = UINT32_MAX - 100;
  manager.configureEnvironment(EnvironmentSensor::Dht11, 44);
  g_mock_millis += 2000;
  CayenneLPP guest(100); manager.querySensors(0, guest); EXPECT_EQ(0, DHT::reads);
  EXPECT_EQ(EnvironmentStatus::Success, manager.readEnvironment().status);
  manager.configureEnvironment(EnvironmentSensor::Dht11, 44); EXPECT_EQ(1, DHT::begins);
  manager.configureEnvironment(EnvironmentSensor::None, 44);
  ASSERT_EQ(1u, dht_released_pins.size()); EXPECT_EQ(44, dht_released_pins[0]);
  g_mock_millis += 2000;
  EXPECT_EQ(EnvironmentStatus::Disabled, manager.readEnvironment().status); EXPECT_EQ(1, DHT::reads);
  manager.configureEnvironment(EnvironmentSensor::Sht4x, 44);
  EXPECT_EQ(EnvironmentStatus::Success, manager.readEnvironment().status);
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
