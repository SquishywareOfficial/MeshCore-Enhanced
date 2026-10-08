#include <gtest/gtest.h>
#include <helpers/EnvironmentCLI.h>
#include "../../src/helpers/SensorManager.h"
#include <limits>

struct Driver {
  int calls = 0, error = 0;
  uint32_t now = 0, duration = 110;
  float temperature = -12.3f, humidity = 48.2f;
  int measureHighPrecision(float& t, float& h) {
    ++calls; now += duration; t = temperature; h = humidity; return error;
  }
};
struct Manager {
  Driver driver;
  bool detected = true;
  EnvironmentReading readEnvironment() {
    return sampleSht4x(driver, detected, [this]() { return driver.now; });
  }
};
TEST(Environment, FormatsFreshPairOncePerCommand) {
  Manager manager; char reply[160];
  EXPECT_TRUE(handleEnvironmentGet(manager, "environment", reply));
  EXPECT_STREQ("> SHT4x@0x44 temperature=-12.3 C humidity=48.2 %RH", reply);
  EXPECT_EQ(1, manager.driver.calls);
  EXPECT_TRUE(handleEnvironmentGet(manager, "temperature", reply));
  EXPECT_STREQ("> -12.3 C", reply);
  EXPECT_TRUE(handleEnvironmentGet(manager, "humidity", reply));
  EXPECT_STREQ("> 48.2 %RH", reply);
  EXPECT_EQ(3, manager.driver.calls);
  EXPECT_FALSE(handleEnvironmentGet(manager, "temperature-extra", reply));
  EXPECT_FALSE(handleEnvironmentGet(manager, "humidity extra", reply));
  EXPECT_EQ(3, manager.driver.calls);
}
TEST(Environment, MissingNeverSamples) {
  Manager manager; manager.detected = false; char reply[160];
  handleEnvironmentGet(manager, "environment", reply);
  EXPECT_STREQ("Error: environment sensor not detected", reply);
  EXPECT_EQ(0, manager.driver.calls);
}
TEST(Environment, FailuresNeverReusePreviousSample) {
  Manager manager; char reply[160];
  handleEnvironmentGet(manager, "environment", reply);
  manager.driver.error = 1;
  handleEnvironmentGet(manager, "environment", reply);
  EXPECT_STREQ("Error: environment sensor read failed", reply);
  manager.driver.error = 0;
  for (float value : {NAN, INFINITY, -INFINITY}) {
    manager.driver.temperature = value;
    handleEnvironmentGet(manager, "temperature", reply);
    EXPECT_STREQ("Error: environment sensor read failed", reply);
    manager.driver.temperature = 22;
    manager.driver.humidity = value;
    handleEnvironmentGet(manager, "humidity", reply);
    EXPECT_STREQ("Error: environment sensor read failed", reply);
    manager.driver.humidity = 48;
  }
}
TEST(Environment, BusTimeoutBoundAndLateMeasurementRejected) {
  struct Bus { uint16_t timeout; uint16_t getTimeOut() { return timeout; }
    void setTimeOut(uint16_t t) { timeout = t; } };
  for (uint16_t initial : {0, 20, 50, 1000}) {
    Bus bus{initial}; boundEnvironmentBusTimeout(bus);
    EXPECT_EQ(initial == 20 ? 20 : 50, bus.timeout);
    // The real driver has one send, 10 ms conversion and one receive.
    Manager manager; manager.driver.duration = 2 * bus.timeout + 10;
    manager.driver.error = 1; char reply[160];
    handleEnvironmentGet(manager, "environment", reply);
    EXPECT_LE(manager.driver.now, 250u);
    EXPECT_EQ(1, manager.driver.calls);
    EXPECT_STREQ("Error: environment sensor read failed", reply);
  }
  Manager manager; manager.driver.duration = 251; char reply[160];
  handleEnvironmentGet(manager, "environment", reply);
  EXPECT_STREQ("Error: environment sensor read failed", reply);
}
TEST(Environment, DefaultManagerIsUnsupported) {
  SensorManager manager; char reply[160];
  handleEnvironmentGet(manager, "environment", reply);
  EXPECT_STREQ("Error: unsupported", reply);
}
TEST(Environment, WraparoundClockAndReplyBounds) {
  Manager manager; manager.driver.now = UINT32_MAX - 20;
  struct { char reply[160]; char sentinel = 'X'; } buffer;
  EXPECT_TRUE(handleEnvironmentGet(manager, "environment", buffer.reply));
  EXPECT_EQ(EnvironmentStatus::Success, manager.readEnvironment().status);
  EXPECT_EQ('X', buffer.sentinel);
  EXPECT_LT(strlen(buffer.reply), 160u);
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
