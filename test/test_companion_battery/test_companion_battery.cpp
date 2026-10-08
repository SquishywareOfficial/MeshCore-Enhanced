#include <gtest/gtest.h>
#include "../../examples/companion_radio/NodePrefs.h"
#include <string>
#include <vector>
#include <limits>

// Exercise the actual companion serializer in its own executable: CommonCLI
// defines a different global NodePrefs and must not be included here.
class TextStream : public Stream {
  size_t position = 0;
  template<class Number> size_t number(Number n) {
    return Print::print(std::to_string(n).c_str());
  }
public:
  std::string text;
  explicit TextStream(const char* input = "") : text(input) {}
  int available() override { return int(text.size() - position); }
  int read() override { return position < text.size() ? uint8_t(text[position++]) : -1; }
  int peek() override { return position < text.size() ? uint8_t(text[position]) : -1; }
  size_t write(uint8_t byte) override { text.push_back(char(byte)); return 1; }
  size_t print(unsigned char n, int) override { return number(n); }
  size_t print(int n, int) override { return number(n); }
  size_t print(unsigned int n, int) override { return number(n); }
  size_t print(long n, int) override { return number(n); }
  size_t print(unsigned long n, int) override { return number(n); }
  size_t print(long long n, int) override { return number(n); }
  size_t print(unsigned long long n, int) override { return number(n); }
  size_t print(double n, int precision) override {
    char value[48]; snprintf(value, sizeof(value), "%.*f", precision, n);
    return Print::print(value);
  }
};

#ifdef XIAO_WIO_BATTERY_CLI
struct Board {
  int gpio = 1, claimed = -1;
  bool connected = false, attach_ok = true;
  float multiplier = 2;
  std::vector<std::string> events;
  bool setAdcMultiplier(float n) {
    events.push_back("calibration");
    if (!std::isfinite(n) || n < 0 || n > 10) return false;
    multiplier = n == 0 ? 2 : n; return true;
  }
  float getAdcMultiplier() const { return multiplier; }
  int getBatteryGpio() const { return gpio; }
  int getBatteryConnected() const { return connected; }
  bool setBatteryGpio(int n) {
    events.push_back("pin:" + std::to_string(n));
    if (n == -1) n = 1;
    if (connected || (n != 1 && n != 2 && n != 4) || n == claimed) return false;
    gpio = n; return true;
  }
  bool setBatteryConnected(bool n) {
    events.push_back(n ? "on" : "off");
    if (n && !attach_ok) return false;
    connected = n; return true;
  }
};
TEST(CompanionBattery, OlderJsonAndLegacyConstructorDefaultsAreDisabled) {
  NodePrefs prefs;
  EXPECT_EQ(0, prefs.battery_connected); EXPECT_EQ(-1, prefs.battery_gpio);
  EXPECT_FLOAT_EQ(0, prefs.adc_multiplier);
  TextStream old("{name:\"Falcz\",radio:{freq:917.375},comp:{pin:123456}}");
  ASSERT_TRUE(prefs.loadSerial(old));
  EXPECT_STREQ("Falcz", prefs.node_name); EXPECT_FLOAT_EQ(917.375f, prefs.freq);
  EXPECT_EQ(123456u, prefs.ble_pin);
  Board board; prefs.applyBatterySettings(board);
  EXPECT_FALSE(board.connected); EXPECT_EQ(1, board.gpio); EXPECT_FLOAT_EQ(2, board.multiplier);
  EXPECT_EQ((std::vector<std::string>{"calibration", "pin:-1", "off"}), board.events);
}
TEST(CompanionBattery, PowerAndUnrelatedSettingsRoundTripThroughExistingSerializer) {
  NodePrefs prefs; strcpy(prefs.node_name, "Test companion");
  prefs.battery_connected = 1; prefs.battery_gpio = 4; prefs.adc_multiplier = 2.05f;
  prefs.freq = 917.375f; prefs.ble_pin = 123456; prefs.telemetry_mode_base = 2;
  prefs.gps_interval = 600; prefs.setRepeatEn(true);
  TextStream output; ASSERT_TRUE(prefs.saveSerial(output));
  EXPECT_NE(std::string::npos, output.text.find("power:{batt_connected:1,batt_gpio:4,adc_mult:2.0500}"));
  NodePrefs restored; TextStream input(output.text.c_str()); ASSERT_TRUE(restored.loadSerial(input));
  EXPECT_EQ(1, restored.battery_connected); EXPECT_EQ(4, restored.battery_gpio);
  EXPECT_FLOAT_EQ(2.05f, restored.adc_multiplier); EXPECT_STREQ(prefs.node_name, restored.node_name);
  EXPECT_FLOAT_EQ(prefs.freq, restored.freq); EXPECT_EQ(prefs.ble_pin, restored.ble_pin);
  EXPECT_EQ(prefs.gps_interval, restored.gps_interval); EXPECT_TRUE(restored.isRepeatEn());
  EXPECT_EQ(prefs.telemetry_mode_base, restored.telemetry_mode_base);
  Board board; restored.applyBatterySettings(board);
  EXPECT_EQ((std::vector<std::string>{"calibration", "pin:4", "on"}), board.events);
  EXPECT_FLOAT_EQ(2.05f, board.multiplier); EXPECT_TRUE(board.connected);
}
TEST(CompanionBattery, SharedSetterSaveCallbackIncludesCompanionPowerFields) {
  NodePrefs prefs; Board board; TextStream saved; int saves = 0; char reply[160];
  auto save = [&]() { ++saves; EXPECT_TRUE(prefs.saveSerial(saved)); };
  EXPECT_TRUE(handleBatteryCommand(board, prefs, "set battery.gpio 2", reply, sizeof(reply), save));
  EXPECT_STREQ("OK", reply); EXPECT_EQ(1, saves);
  NodePrefs restored; TextStream input(saved.text.c_str()); ASSERT_TRUE(restored.loadSerial(input));
  EXPECT_EQ(2, restored.battery_gpio); EXPECT_EQ(0, restored.battery_connected);
}
TEST(CompanionBattery, InvalidSavedCalibrationFallsBackBeforeSensing) {
  for (float value : {-1.0f, 11.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
    NodePrefs prefs; prefs.adc_multiplier = value; prefs.battery_connected = 1; prefs.battery_gpio = 2;
    Board board; prefs.applyBatterySettings(board);
    EXPECT_EQ((std::vector<std::string>{"calibration", "calibration", "pin:2", "on"}), board.events);
    EXPECT_FLOAT_EQ(2, board.multiplier); EXPECT_TRUE(board.connected);
  }
}
TEST(CompanionBattery, InvalidGpioAndEnableValuesCannotStartSensing) {
  for (const char* json : {"{power:{batt_gpio:3,batt_connected:1}}", "{power:{batt_gpio:257,batt_connected:1}}", "{power:{batt_gpio:2,batt_connected:2}}", "{power:{batt_gpio:2,batt_connected:257}}", "{power:{batt_gpio:2,batt_connected:-255}}"}) {
    NodePrefs prefs; TextStream input(json); ASSERT_TRUE(prefs.loadSerial(input));
    Board board; prefs.applyBatterySettings(board);
    EXPECT_FALSE(board.connected) << json;
  }
}
TEST(CompanionBattery, ClaimedPinAndAttachmentFailureRemainDisabled) {
  NodePrefs prefs; prefs.battery_gpio = 2; prefs.battery_connected = 1;
  Board claimed; claimed.claimed = 2; prefs.applyBatterySettings(claimed);
  EXPECT_FALSE(claimed.connected); EXPECT_EQ(1, claimed.gpio);
  Board failed; failed.attach_ok = false; prefs.applyBatterySettings(failed);
  EXPECT_FALSE(failed.connected);
}
TEST(CompanionBattery, StartupDoesNotAlterPersistedValuesOrSaveSettings) {
  NodePrefs prefs; prefs.battery_gpio = 3; prefs.battery_connected = 1; prefs.adc_multiplier = 11;
  Board board; prefs.applyBatterySettings(board);
  EXPECT_EQ(3, prefs.battery_gpio); EXPECT_EQ(1, prefs.battery_connected);
  EXPECT_FLOAT_EQ(11, prefs.adc_multiplier); EXPECT_FALSE(board.connected);
}
#else
TEST(CompanionBatteryDisabled, SerializerRetainsExistingShapeAndIgnoresPower) {
  NodePrefs prefs; TextStream input("{name:\"Existing\",comp:{pin:123456},power:{batt_connected:1,batt_gpio:4,adc_mult:2.05}}");
  ASSERT_TRUE(prefs.loadSerial(input));
  EXPECT_STREQ("Existing", prefs.node_name); EXPECT_EQ(123456u, prefs.ble_pin);
  TextStream saved; ASSERT_TRUE(prefs.saveSerial(saved));
  EXPECT_EQ(std::string::npos, saved.text.find("power:"));
}
#endif
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
