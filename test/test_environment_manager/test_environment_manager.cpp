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
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
