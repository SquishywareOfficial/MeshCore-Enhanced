#include <gtest/gtest.h>
#include <helpers/BatteryCLI.h>
#include <string>
#include <vector>

struct BatteryPrefs {
  int8_t battery_gpio = -1;
  uint8_t battery_connected = 0;
  float adc_multiplier = 0;
};
struct BatteryBoard {
  bool battery_support = true, multiplier_support = true, attach_ok = true;
  bool connected = false;
  int gpio = 1, claimed = -1;
  float multiplier = 2;
  int multiplier_calls = 0;
  std::vector<std::string> events;
  int getBatteryGpio() const { return battery_support ? gpio : -1; }
  int getBatteryConnected() const { return battery_support ? connected : -1; }
  float getAdcMultiplier() const { return multiplier_support ? multiplier : 0; }
  bool setBatteryGpio(int pin) {
    events.push_back("pin:" + std::to_string(pin));
    if (!battery_support || connected) return false;
    if (pin == -1) pin = 1;
    if ((pin != 1 && pin != 2 && pin != 4) || pin == claimed) return false;
    gpio = pin; return true;
  }
  bool setBatteryConnected(bool on) {
    events.push_back(on ? "on" : "off");
    if (!battery_support || (on && !attach_ok)) return false;
    connected = on; return true;
  }
  bool setAdcMultiplier(float value) {
    ++multiplier_calls;
    if (!multiplier_support) return false;
    multiplier = value == 0 ? 2 : value; return true;
  }
};
class BatteryCLI : public ::testing::Test {
protected:
  BatteryBoard board;
  BatteryPrefs prefs;
  int saves = 0;
  char reply[160] = {};
  std::string run(const char* command) {
    EXPECT_TRUE(handleBatteryCommand(board, prefs, command, reply, sizeof(reply), [this]() { ++saves; }));
    return reply;
  }
};
TEST_F(BatteryCLI, GettersAndUnrelatedCommandsDoNotChangeSettings) {
  EXPECT_EQ("> off", run("get battery.connected"));
  EXPECT_EQ("> 1", run("get battery.gpio"));
  EXPECT_EQ("> 2.000", run("get adc.multiplier"));
  for (const char* command : {"get environment", "get adc.multiplier.extra", "set battery.gpio-extra 2", "output on 2"}) {
    EXPECT_FALSE(handleBatteryCommand(board, prefs, command, reply, sizeof(reply), [this]() { ++saves; }));
  }
  EXPECT_EQ(0, saves); EXPECT_TRUE(board.events.empty());
}
TEST_F(BatteryCLI, AllowedPinsDefaultAndEnableStateAreSaved) {
  for (const char* command : {"set battery.gpio 1", "set battery.gpio 2", "set battery.gpio 4"}) EXPECT_EQ("OK", run(command));
  EXPECT_EQ(4, prefs.battery_gpio); EXPECT_EQ("> 4", run("get battery.gpio"));
  EXPECT_EQ("OK", run("set battery.gpio default"));
  EXPECT_EQ(-1, prefs.battery_gpio); EXPECT_EQ(1, board.gpio);
  EXPECT_EQ("OK", run("set battery.connected on"));
  EXPECT_EQ(1, prefs.battery_connected); EXPECT_EQ("> on", run("get battery.connected"));
  EXPECT_EQ("Error: turn battery.connected off first", run("set battery.gpio 4"));
  EXPECT_EQ("OK", run("set battery.connected off"));
  EXPECT_EQ(0, prefs.battery_connected); EXPECT_EQ(6, saves);
}
TEST_F(BatteryCLI, InvalidOrClaimedPinsDoNotSave) {
  board.claimed = 2;
  for (const char* command : {"set battery.gpio", "set battery.gpio ", "set battery.gpio -1", "set battery.gpio 3", "set battery.gpio 2", "set battery.gpio 99999999999999999999", "set battery.gpio 4x", "set battery.gpio 4 "}) {
    EXPECT_EQ("Error: invalid battery GPIO", run(command));
    EXPECT_EQ(-1, prefs.battery_gpio); EXPECT_EQ(1, board.gpio);
  }
  EXPECT_EQ(0, saves);
}
TEST_F(BatteryCLI, InvalidSwitchesAndAttachmentFailureDoNotSave) {
  for (const char* command : {"set battery.connected", "set battery.connected ON", "set battery.connected on extra"})
    EXPECT_EQ("Error: expected on or off", run(command));
  EXPECT_TRUE(board.events.empty());
  board.attach_ok = false;
  EXPECT_EQ("Error: unsupported", run("set battery.connected on"));
  EXPECT_FALSE(board.connected); EXPECT_EQ(0, prefs.battery_connected); EXPECT_EQ(0, saves);
}
TEST_F(BatteryCLI, CalibrationPreservesSuccessRepliesAndDefaultReset) {
  EXPECT_EQ("OK - multiplier set to 2.050", run("set adc.multiplier 2.05"));
  EXPECT_FLOAT_EQ(2.05f, prefs.adc_multiplier);
  EXPECT_EQ("> 2.050", run("get adc.multiplier"));
  EXPECT_EQ("OK - multiplier set to 10.000", run("set adc.multiplier 10"));
  EXPECT_EQ("OK - using default board multiplier", run("set adc.multiplier 0"));
  EXPECT_FLOAT_EQ(0, prefs.adc_multiplier); EXPECT_FLOAT_EQ(2, board.multiplier); EXPECT_EQ(3, saves);
}
TEST_F(BatteryCLI, MalformedCalibrationCannotResetOrOverwritePreviousValue) {
  run("set adc.multiplier 2.05");
  for (const char* argument : {"", "abc", "2.0junk", "2.0 ", "-1", "11", "nan", "inf", "-inf", "1e100", "1e-100"}) {
    const std::string command = std::string("set adc.multiplier ") + argument;
    EXPECT_EQ("Error: invalid multiplier", run(command.c_str())) << argument;
    EXPECT_FLOAT_EQ(2.05f, prefs.adc_multiplier); EXPECT_FLOAT_EQ(2.05f, board.multiplier);
  }
  EXPECT_EQ(1, saves); EXPECT_EQ(1, board.multiplier_calls);
}
TEST_F(BatteryCLI, MultiplierOnlyBoardsRetainCalibrationAndRejectedHooksKeepPrefs) {
  board.battery_support = false;
  EXPECT_EQ("Error: unsupported", run("get battery.connected"));
  EXPECT_EQ("Error: unsupported", run("get battery.gpio"));
  EXPECT_EQ("Error: unsupported", run("set battery.gpio 2"));
  EXPECT_EQ("Error: unsupported", run("set battery.connected on"));
  EXPECT_EQ("OK - multiplier set to 2.100", run("set adc.multiplier 2.1"));
  board.multiplier_support = false;
  EXPECT_EQ("Error: unsupported", run("get adc.multiplier"));
  EXPECT_EQ("Error: unsupported", run("set adc.multiplier 3"));
  EXPECT_FLOAT_EQ(2.1f, prefs.adc_multiplier); EXPECT_EQ(1, saves);
}
TEST_F(BatteryCLI, RepliesRespectRemainingCapacityIncludingZero) {
  char bounded[] = {'A', 'B', 'C', 'D', 'E', 'F'};
  EXPECT_TRUE(handleBatteryCommand(board, prefs, "get battery.gpio", bounded + 1, 3, []() {}));
  EXPECT_EQ('A', bounded[0]); EXPECT_STREQ("> ", bounded + 1); EXPECT_EQ('E', bounded[4]);
  EXPECT_TRUE(handleBatteryCommand(board, prefs, "get adc.multiplier", nullptr, 0, []() {}));
  EXPECT_EQ(0, saves);
}
TEST_F(BatteryCLI, RestoreUsesOnlyBatteryFieldsAndResolvesSelectionBeforeEnabling) {
  prefs.battery_gpio = 4; prefs.battery_connected = 1;
  restoreBatterySettings(board, prefs);
  EXPECT_EQ((std::vector<std::string>{"pin:4", "on"}), board.events);
  EXPECT_EQ(4, board.gpio); EXPECT_TRUE(board.connected);
  BatteryBoard invalid; prefs.battery_gpio = 3;
  restoreBatterySettings(invalid, prefs);
  EXPECT_EQ((std::vector<std::string>{"pin:3", "pin:-1", "off"}), invalid.events);
  EXPECT_EQ(1, invalid.gpio); EXPECT_FALSE(invalid.connected);
  BatteryBoard bad_flag; prefs.battery_gpio = -1; prefs.battery_connected = 2;
  restoreBatterySettings(bad_flag, prefs); EXPECT_FALSE(bad_flag.connected);
  BatteryBoard failed; failed.attach_ok = false; prefs.battery_connected = 1;
  restoreBatterySettings(failed, prefs); EXPECT_FALSE(failed.connected);
  EXPECT_EQ(0, saves);
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
