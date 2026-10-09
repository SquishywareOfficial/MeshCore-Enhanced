#include <gtest/gtest.h>
#include <MeshCore.h>
#include <helpers/OutputCLI.h>
#include <helpers/EnvironmentConfigCLI.h>
#include <helpers/SensorManager.h>
#include <helpers/RepeaterCLIAccess.h>
#include <string>
#include <vector>
class ESP32Board : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 0; }
  const char* getManufacturerName() const override { return "mock"; }
  void reboot() override {}
  uint8_t getStartupReason() const override { return 0; }
};
#define D0 1
#define D1 2
#define D3 4
#define BATTERY_ADC_PIN D0
#define XIAO_WIO_OUTPUTS 1
#define XIAO_WIO_ENVIRONMENT 1
#define INPUT 1
#define OUTPUT 2
#define LOW 0
#define HIGH 1
#define ADC_11db 3
// Also compile this suite with -DSERIAL_TX=43 -DSERIAL_RX=44 to verify
// active UART ownership using the same production board implementation.
struct Event {
  char operation; int pin, value;
  bool operator==(const Event& e) const {
    return operation == e.operation && pin == e.pin && value == e.value;
  }
};
static std::vector<Event> events;
void pinMode(int pin, int mode) { events.push_back({'m', pin, mode}); }
void digitalWrite(int pin, int value) { events.push_back({'w', pin, value}); }
bool adcAttachPin(int) { return true; }
void analogSetPinAttenuation(int,int) {}
void analogReadResolution(int) {}
uint32_t analogReadMilliVolts(int) { return 2100; }
#include "../../variants/xiao_s3_wio/XiaoS3WIOBoard.h"
struct Prefs {
  int8_t battery_gpio = -1, environment_gpio = 2;
  uint8_t battery_connected = 0, outputs_mask = 0, environment_sensor = 0;
  float adc_multiplier = 0;
};
class Outputs : public ::testing::Test {
protected:
  XiaoS3WIOBoard board;
  Prefs prefs;
  int saves = 0;
  char reply[160];
  void SetUp() override { events.clear(); }
  std::string command(const char* cmd) {
    EXPECT_TRUE(handleOutputCommand(board, prefs.outputs_mask, cmd, reply, [this]() { ++saves; }));
    return reply;
  }
};
TEST_F(Outputs, ConcreteBatteryConflictAndRestartScenario) {
  EXPECT_EQ("Error: GPIO reserved", command("output configure 1"));
  EXPECT_TRUE(events.empty()); EXPECT_EQ(0, saves);
  EXPECT_EQ("OK", command("output configure 2"));
  EXPECT_EQ((std::vector<Event>{{'w',2,LOW},{'m',2,OUTPUT}}), events);
  EXPECT_EQ("OK", command("output on 2"));
  events.clear();
  EXPECT_EQ("OK", command("output configure 2"));
  EXPECT_EQ("OK", command("output on 2"));
  EXPECT_TRUE(events.empty()); EXPECT_EQ(1, saves);
  EXPECT_FALSE(board.setBatteryGpio(2));
  EXPECT_EQ(1, board.getBatteryGpio()); EXPECT_EQ(1, board.getOutputState(2));
  XiaoS3WIOBoard rebooted; restoreOptionalIO(rebooted, prefs);
  EXPECT_EQ(1, rebooted.getBatteryGpio()); EXPECT_EQ(0, rebooted.getOutputState(2));
  EXPECT_EQ((std::vector<Event>{{'w',2,LOW},{'m',2,OUTPUT}}), events);
  events.clear();
  EXPECT_EQ("OK", command("output remove 2"));
  EXPECT_EQ((std::vector<Event>{{'w',2,LOW},{'m',2,INPUT}}), events);
  events.clear(); EXPECT_EQ(2, saves);
  EXPECT_TRUE(board.setBatteryGpio(2)); EXPECT_TRUE(events.empty());
  EXPECT_EQ("OK", command("output configure 1"));
  EXPECT_FALSE(board.setBatteryGpio(-1)); EXPECT_EQ(2, board.getBatteryGpio());
  EXPECT_EQ(0, board.getOutputState(1));
  events.clear();
  EXPECT_EQ("OK", command("output remove 2")); // now battery owned and absent
  EXPECT_TRUE(events.empty()); EXPECT_EQ(3, saves);
}
TEST_F(Outputs, EveryFreePinAndSortedStatus) {
  EXPECT_EQ("OK", command("output configure 4"));
  EXPECT_EQ("OK", command("output configure 2"));
#if !defined(SERIAL_TX) && !defined(SERIAL_RX)
  EXPECT_EQ("OK", command("output configure 44"));
  EXPECT_EQ("OK", command("output configure 43"));
  EXPECT_EQ(30, prefs.outputs_mask); // fixed indexes support GPIO43/44
  EXPECT_EQ("OK", command("output on 43"));
  EXPECT_EQ("> 2=off 4=off 43=on 44=off", command("output status"));
  EXPECT_EQ(4, saves);
  EXPECT_EQ("OK", command("output off 43"));
  events.clear();
  EXPECT_EQ("OK", command("output off 43"));
  EXPECT_TRUE(events.empty()); EXPECT_EQ(4, saves);
#else
  for (const char* cmd : {"output configure 43", "output on 43", "output configure 44"})
    EXPECT_EQ("Error: GPIO reserved", command(cmd));
  EXPECT_EQ(2, saves);
#endif
}
TEST_F(Outputs, MalformedAndReservedCommandsNeverTouchHardwareOrPrefs) {
  for (const char* cmd : {"output", "output status extra", "output on", "output on ",
      "output on -2", "output on +2", "output on 0x2", "output configure D1",
      "output configure 2extra", "output configure 2 4", "output on 2 ",
      "output on  2", "output unknown 2", "output configure 999999999999x"})
    EXPECT_EQ("Error: usage", command(cmd));
  for (int pin : {0,3,5,6,7,8,9,21,38,39,40,41,42,48,127}) {
    const auto cmd = "output configure " + std::to_string(pin);
    EXPECT_EQ("Error: invalid GPIO", command(cmd.c_str()));
  }
  EXPECT_EQ("Error: invalid GPIO", command("output configure 999999999999999999"));
  EXPECT_EQ("Error: output not configured", command("output on 2"));
  EXPECT_EQ("Error: output not configured", command("output off 4"));
  EXPECT_EQ("> none", command("output status"));
  EXPECT_TRUE(events.empty()); EXPECT_EQ(0, saves); EXPECT_EQ(0, prefs.outputs_mask);
}
TEST_F(Outputs, CorruptSavedMasksReserveBatteryFirstAndDoNotSave) {
  prefs.battery_gpio = 4; prefs.outputs_mask = 255;
  restoreOptionalIO(board, prefs);
#if !defined(SERIAL_TX) && !defined(SERIAL_RX)
  EXPECT_EQ(27, prefs.outputs_mask);
  EXPECT_EQ(8u, events.size());
#else
  EXPECT_EQ(3, prefs.outputs_mask);
  EXPECT_EQ(4u, events.size());
#endif
  EXPECT_EQ(4, board.getBatteryGpio());
  for (const auto& e : events) EXPECT_NE(4, e.pin);
  for (size_t i = 0; i < events.size(); i += 2) {
    EXPECT_EQ('w', events[i].operation); EXPECT_EQ(LOW, events[i].value);
    EXPECT_EQ('m', events[i+1].operation); EXPECT_EQ(OUTPUT, events[i+1].value);
  }
  EXPECT_EQ(0, saves);
  events.clear(); XiaoS3WIOBoard corrupt; prefs.battery_gpio = 127;
  prefs.battery_connected = 1; prefs.outputs_mask = 255;
  restoreOptionalIO(corrupt, prefs);
  EXPECT_EQ(1, corrupt.getBatteryGpio()); EXPECT_EQ(0, corrupt.getBatteryConnected());
  for (const auto& e : events) EXPECT_NE(1, e.pin);
  EXPECT_EQ(0, corrupt.getOutputState(2));
}
TEST_F(Outputs, UnsupportedBoardHasNoEffect) {
  ESP32Board unsupported; uint8_t stored = 16; int saved = 0;
  for (const char* cmd : {"output configure 2", "output on 2", "output off 2",
                         "output remove 2", "output status"}) {
    handleOutputCommand(unsupported, stored, cmd, reply, [&]() { ++saved; });
    EXPECT_STREQ("Error: unsupported", reply);
  }
  EXPECT_EQ(16, stored); EXPECT_EQ(0, saved); EXPECT_TRUE(events.empty());
}
TEST_F(Outputs, RemoteGuestBoundaryCannotDispatchOutputs) {
  for (bool admin : {false, true}) {
    if (acceptsRepeaterCLI(true, 20, admin)) command("output configure 2");
    EXPECT_EQ(admin ? 1 : 0, saves);
    EXPECT_EQ(admin ? 2u : 0u, events.size());
  }
  EXPECT_FALSE(acceptsRepeaterCLI(false, 20, true));
  EXPECT_FALSE(acceptsRepeaterCLI(true, 5, true));
}
TEST_F(Outputs, ReplyBoundsAndUnrelatedCommands) {
  struct { char reply[160]; char sentinel = 'X'; } buffer;
  EXPECT_TRUE(handleOutputCommand(board, prefs.outputs_mask, "output status",
    buffer.reply, []() {})); EXPECT_EQ('X', buffer.sentinel);
  EXPECT_FALSE(handleOutputCommand(board, prefs.outputs_mask, "outputs", reply, []() {}));
  EXPECT_TRUE(events.empty());
}
TEST_F(Outputs, BatteryCommandParityAllPinsAndCalibration) {
  auto battery = [this](const char* cmd) {
    EXPECT_TRUE(handleBatteryCommand(board, prefs, cmd, reply, sizeof(reply), [this]() { ++saves; }));
    return std::string(reply);
  };
  EXPECT_EQ("> off", battery("get battery.connected"));
  for (int pin : {1,2,4}) {
    EXPECT_EQ("OK", battery(("set battery.gpio " + std::to_string(pin)).c_str()));
    EXPECT_EQ("> " + std::to_string(pin), battery("get battery.gpio"));
    EXPECT_EQ("OK", battery("set battery.connected on"));
    EXPECT_EQ("> on", battery("get battery.connected"));
    EXPECT_EQ(4200, board.getBattMilliVolts());
    EXPECT_EQ("Error: turn battery.connected off first", battery("set battery.gpio default"));
    EXPECT_EQ("OK", battery("set battery.connected off"));
    EXPECT_EQ(0, board.getBattMilliVolts());
  }
  EXPECT_EQ("OK", battery("set battery.gpio default"));
  EXPECT_EQ(-1, prefs.battery_gpio); EXPECT_EQ(1, board.getBatteryGpio());
  EXPECT_EQ("OK - multiplier set to 2.050", battery("set adc.multiplier 2.05"));
  EXPECT_EQ("> 2.050", battery("get adc.multiplier"));
  EXPECT_EQ("OK - using default board multiplier", battery("set adc.multiplier 0"));
}
TEST_F(Outputs, IndependentOutputsAndAssignmentsRestoreOff) {
  EXPECT_EQ("OK", command("output configure 2"));
  EXPECT_EQ("OK", command("output configure 4"));
  EXPECT_EQ("OK", command("output on 2"));
  EXPECT_EQ("> 2=on 4=off", command("output status"));
  EXPECT_EQ("OK", command("output on 4"));
  EXPECT_EQ("OK", command("output off 2"));
  EXPECT_EQ("> 2=off 4=on", command("output status"));
  EXPECT_EQ(2, saves);
  XiaoS3WIOBoard restored; restoreOptionalIO(restored, prefs);
  EXPECT_EQ(0, restored.getOutputState(2)); EXPECT_EQ(0, restored.getOutputState(4));
  EXPECT_EQ(6, prefs.outputs_mask); EXPECT_EQ(2, saves);
}
TEST_F(Outputs, BatteryAndOutputCommandOwnershipWorksBothDirections) {
  auto battery = [this](const char* cmd) {
    EXPECT_TRUE(handleBatteryCommand(board, prefs, cmd, reply, sizeof(reply), [this]() { ++saves; }));
    return std::string(reply);
  };
  EXPECT_EQ("Error: GPIO reserved", command("output configure 1"));
  EXPECT_EQ("OK", command("output configure 2"));
  EXPECT_EQ("Error: invalid battery GPIO", battery("set battery.gpio 2"));
  EXPECT_EQ("OK", command("output remove 2"));
  EXPECT_EQ("OK", battery("set battery.gpio 2"));
  EXPECT_EQ("OK", command("output configure 1"));
  EXPECT_EQ("Error: GPIO reserved", command("output configure 2"));
  EXPECT_EQ("OK", battery("set battery.connected on"));
  const int saved = saves;
  EXPECT_EQ("OK", command("output on 1"));
  EXPECT_EQ("OK", command("output off 1"));
  EXPECT_EQ("> 1=off", command("output status"));
  EXPECT_EQ(saved, saves); EXPECT_EQ(4200, board.getBattMilliVolts());
}
TEST_F(Outputs, NonDefaultBatteryAndOutputsRestoreTogether) {
  prefs.battery_gpio = 4; prefs.battery_connected = 1; prefs.adc_multiplier = 2.05f;
  prefs.outputs_mask = 3; // GPIO1 and GPIO2; GPIO4 belongs to the ADC.
  restoreOptionalIO(board, prefs);
  ASSERT_TRUE(board.setAdcMultiplier(prefs.adc_multiplier));
  EXPECT_EQ(4, board.getBatteryGpio()); EXPECT_EQ(1, board.getBatteryConnected());
  EXPECT_NEAR(4305.0, board.getBattMilliVolts(), 1.0);
  EXPECT_EQ(0, board.getOutputState(1)); EXPECT_EQ(0, board.getOutputState(2));
  EXPECT_EQ(0, saves);
}

struct ConfigManager : SensorManager {
  int type = 0, gpio = 2, configurations = 0;
  int getEnvironmentSensor() const override { return type; }
  int getEnvironmentGpio() const override { return gpio; }
  bool configureEnvironment(EnvironmentSensor sensor, int pin) override {
    type = int(sensor); gpio = pin; ++configurations; return true;
  }
};
TEST_F(Outputs, EnvironmentCommandsPersistAndProtectPinBothWays) {
  ConfigManager manager;
  auto env = [&](const char* cmd) {
    EXPECT_TRUE(handleEnvironmentConfigCommand(board, manager, prefs, cmd, reply, [&]() { ++saves; }));
    return std::string(reply);
  };
  EXPECT_EQ("> auto", env("get environment.sensor")); EXPECT_EQ("> 2", env("get environment.gpio"));
  EXPECT_EQ(0, saves); EXPECT_TRUE(events.empty());
  EXPECT_EQ("OK", env("set environment.sensor dht11"));
  EXPECT_EQ(3, prefs.environment_sensor); EXPECT_EQ(2, prefs.environment_gpio);
  EXPECT_EQ("Error: GPIO reserved", command("output configure 2"));
  EXPECT_FALSE(board.setBatteryGpio(2));
  EXPECT_EQ("Error: set environment.sensor none before changing GPIO", env("set environment.gpio 4"));
  EXPECT_EQ("OK", env("set environment.sensor none"));
  EXPECT_EQ("OK", command("output configure 2"));
  const int saved = saves;
  EXPECT_EQ("Error: GPIO unavailable", env("set environment.sensor dht11"));
  EXPECT_EQ(saved, saves); EXPECT_EQ(1, manager.type);
  EXPECT_EQ("OK", env("set environment.gpio 4"));
  EXPECT_EQ("OK", env("set environment.sensor dht11"));
  EXPECT_EQ(4, manager.gpio); EXPECT_EQ("Error: GPIO reserved", command("output configure 4"));
}
TEST_F(Outputs, EnvironmentRejectsUnsafeMalformedAndClaimedPinsWithoutSaving) {
  ConfigManager manager;
  for (const char* cmd : {"set environment.gpio", "set environment.gpio -1", "set environment.gpio 2x",
                        "set environment.gpio 2 extra", "set environment.gpio 999999999999999999999",
                        "set environment.gpio 0", "set environment.gpio 3", "set environment.gpio 5",
                        "set environment.gpio 39", "set environment.gpio 1", "set environment.sensor dht22"}) {
    EXPECT_TRUE(handleEnvironmentConfigCommand(board, manager, prefs, cmd, reply, [&]() { ++saves; }));
    EXPECT_EQ(0, strncmp(reply, "Error:", 6)) << cmd;
    EXPECT_EQ(0, saves); EXPECT_TRUE(events.empty()); EXPECT_EQ(0, manager.configurations);
  }
#ifdef SERIAL_TX
  EXPECT_TRUE(handleEnvironmentConfigCommand(board, manager, prefs, "set environment.gpio 43", reply, [&]() { ++saves; }));
  EXPECT_STREQ("Error: GPIO unavailable", reply); EXPECT_EQ(0, saves);
#endif
  EXPECT_FALSE(handleEnvironmentConfigCommand(board, manager, prefs, "get environment.sensor.extra", reply, []() {}));
  SensorManager unsupported;
  EXPECT_TRUE(handleEnvironmentConfigCommand(board, unsupported, prefs, "get environment.sensor", reply, []() {}));
  EXPECT_STREQ("Error: unsupported", reply);
}
TEST_F(Outputs, EnvironmentStartupClaimsBeforeOutputs) {
  ConfigManager manager; prefs.environment_sensor = 3; prefs.environment_gpio = 2;
  prefs.outputs_mask = 6; // GPIO2 + GPIO4
  restoreBatterySettings(board, prefs);
  restoreEnvironmentSettings(board, manager, prefs);
  prefs.outputs_mask = board.restoreOutputs(prefs.outputs_mask);
  EXPECT_EQ(3, manager.type); EXPECT_EQ(4, prefs.outputs_mask);
  EXPECT_EQ(-1, board.getOutputState(2)); EXPECT_EQ(0, board.getOutputState(4));
  for (const auto& event : events) EXPECT_NE(2, event.pin);
  EXPECT_FALSE(board.setBatteryGpio(2)); EXPECT_EQ(0, saves);
}
TEST_F(Outputs, EnvironmentStartupBadPreferencesCannotTakeBatteryOrUnsafePin) {
  for (int pin : {1, 21, 39, -1}) {
    XiaoS3WIOBoard fresh; ConfigManager manager;
    prefs.environment_sensor = 3; prefs.environment_gpio = pin;
    restoreBatterySettings(fresh, prefs);
    restoreEnvironmentSettings(fresh, manager, prefs);
    EXPECT_EQ(1, manager.type); EXPECT_EQ(1, prefs.environment_sensor);
    EXPECT_TRUE(events.empty()); EXPECT_TRUE(fresh.setBatteryGpio(2));
  }
  ConfigManager manager; prefs.environment_sensor = 255; prefs.environment_gpio = 2;
  restoreEnvironmentSettings(board, manager, prefs); EXPECT_EQ(1, manager.type);
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
