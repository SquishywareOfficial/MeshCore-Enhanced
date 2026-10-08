#ifndef XIAO_WIO_BATTERY_CLI
#define XIAO_WIO_BATTERY_CLI 1
#endif
#include <gtest/gtest.h>
#include <MeshCore.h>
#include <helpers/CompanionBatteryCLI.h>
#include "../../examples/companion_radio/NodePrefs.h"
#include <string>
#include <cstring>

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
#define INPUT 1
#define ADC_11db 3
static int reads, pin_calls, attached_pin;
static bool attach_ok;
void pinMode(int, int) { ++pin_calls; }
bool adcAttachPin(int pin) { attached_pin = pin; return attach_ok; }
void analogSetPinAttenuation(int, int) {}
void analogReadResolution(int) {}
uint32_t analogReadMilliVolts(int) { ++reads; return 2000; }
#include "../../variants/xiao_s3_wio/XiaoS3WIOBoard.h"

class CompanionCLI : public ::testing::Test {
protected:
  XiaoS3WIOBoard board;
  NodePrefs prefs;
  int saves = 0;
  uint8_t output[MAX_FRAME_SIZE + 1];
  void SetUp() override { reads = pin_calls = attached_pin = 0; attach_ok = true; }
  std::string send(std::string text, bool terminated = true) {
    uint8_t frame[MAX_FRAME_SIZE + 1] = {COMPANION_CMD_RUN_CLI_COMMAND};
    size_t length = 1 + text.size() + (terminated ? 1 : 0);
    EXPECT_LE(length, sizeof(frame));
    if (length > sizeof(frame)) return "test frame too large";
    memcpy(frame + 1, text.data(), text.size());
    memset(output, 0xA5, sizeof(output));
    const size_t n = handleCompanionBatteryCLIFrame(board, prefs, frame, length,
      output, sizeof(output), [this]() { ++saves; });
    EXPECT_GT(n, 0u); EXPECT_LE(n, MAX_FRAME_SIZE);
    EXPECT_EQ(COMPANION_RESP_CODE_CLI_REPLY, output[0]);
    EXPECT_EQ(0xA5, output[n]);
    return std::string(reinterpret_cast<char*>(output + 1), n - 1);
  }
};
TEST_F(CompanionCLI, AppFramingAndCorrelatedGetterDoNotTouchHardwareOrSave) {
  EXPECT_EQ("01|> off", send("01|get battery.connected"));
  EXPECT_EQ("ff|> 1", send("ff|get battery.gpio"));
  EXPECT_EQ("02|> 2.000", send("02|get adc.multiplier"));
  EXPECT_EQ(0, saves); EXPECT_EQ(0, pin_calls); EXPECT_EQ(0, reads);
}
TEST_F(CompanionCLI, FramedSettersUseActualBoardAndCorrectVoltageOnce) {
  EXPECT_EQ("01|OK", send("01|set battery.gpio 2"));
  EXPECT_EQ("02|OK - multiplier set to 2.100", send("02|set adc.multiplier 2.1"));
  EXPECT_EQ("03|OK", send("03|set battery.connected on"));
  EXPECT_EQ(2, attached_pin); EXPECT_EQ(1, prefs.battery_connected);
  EXPECT_EQ(2, prefs.battery_gpio); EXPECT_FLOAT_EQ(2.1f, prefs.adc_multiplier);
  EXPECT_EQ(4200, board.getBattMilliVolts()); EXPECT_EQ(4, reads); EXPECT_EQ(3, saves);
  EXPECT_EQ("04|OK", send("04|set battery.connected off"));
  reads = 0; EXPECT_EQ(0, board.getBattMilliVolts()); EXPECT_EQ(0, reads);
}
TEST_F(CompanionCLI, MissingTerminatorWhitespaceAndZeroPaddingAreSupported) {
  EXPECT_EQ("> off", send("get battery.connected", false));
  EXPECT_EQ("ab|> off", send("  ab|   get battery.connected", false));
  std::string text = "ab|get battery.gpio"; text.append(4, '\0');
  EXPECT_EQ("ab|> 1", send(text));
}
TEST_F(CompanionCLI, ErrorRepliesPreserveRequestPrefixAndPreferences) {
  EXPECT_EQ("01|Error: invalid battery GPIO", send("01|set battery.gpio 3"));
  EXPECT_EQ("02|Error: invalid multiplier", send("02|set adc.multiplier 2.0junk"));
  attach_ok = false;
  EXPECT_EQ("03|Error: unsupported", send("03|set battery.connected on"));
  EXPECT_EQ(-1, prefs.battery_gpio); EXPECT_EQ(0, prefs.battery_connected);
  EXPECT_EQ(0, saves); EXPECT_EQ(0, reads);
}
TEST_F(CompanionCLI, UnknownCommandsCannotAddOutputsOrEnvironmentControls) {
  for (const char* command : {"get name", "get public.key", "ver", "get environment", "output on 2"})
    EXPECT_EQ("ab|Unknown command", send(std::string("ab|") + command));
  EXPECT_EQ(0, saves); EXPECT_EQ(0, pin_calls);
}
TEST_F(CompanionCLI, EmptyAndEmbeddedNulRequestsCannotRunPartialSetters) {
  for (const char* text : {"", "  "}) EXPECT_EQ("Error: empty command", send(text));
  EXPECT_EQ("ab|Error: empty command", send("ab|   "));
  std::string text = "ab|set battery.connected on"; text += '\0'; text += "junk";
  EXPECT_EQ("ab|Error: invalid command frame", send(text));
  EXPECT_EQ(0, saves); EXPECT_EQ(0, pin_calls); EXPECT_EQ(0, prefs.battery_connected);
}
TEST_F(CompanionCLI, MaximumFramesAreBoundedAndOversizedFramesRejected) {
  const std::string command = "ab|get battery.gpio";
  EXPECT_EQ("ab|> 1", send(std::string(MAX_FRAME_SIZE - 1 - command.size(), ' ') + command, false));
  EXPECT_EQ("Error: invalid command frame", send(std::string(MAX_FRAME_SIZE, ' '), false));
  EXPECT_EQ(0, saves);
}
TEST_F(CompanionCLI, ShortOrUnrelatedFramesAndSmallReplyBuffersHaveNoEffects) {
  uint8_t frame[MAX_FRAME_SIZE] = {COMPANION_CMD_RUN_CLI_COMMAND};
  auto save = [this]() { ++saves; };
  EXPECT_EQ(0u, handleCompanionBatteryCLIFrame(board, prefs, frame, 0, output, sizeof(output), save));
  frame[0] = 20;
  EXPECT_EQ(0u, handleCompanionBatteryCLIFrame(board, prefs, frame, 1, output, sizeof(output), save));
  frame[0] = COMPANION_CMD_RUN_CLI_COMMAND;
  EXPECT_GT(handleCompanionBatteryCLIFrame(board, prefs, frame, 1, output, sizeof(output), save), 0u);
  const char* command = "ab|set battery.connected on"; memcpy(frame + 1, command, strlen(command));
  EXPECT_EQ(0u, handleCompanionBatteryCLIFrame(board, prefs, frame, 1 + strlen(command), output,
    COMPANION_CLI_MAX_REPLY - 1, save));
  EXPECT_EQ(0, saves); EXPECT_EQ(0, pin_calls);
}
TEST_F(CompanionCLI, ReplyWithExactCapacityDoesNotOverwriteGuards) {
  uint8_t guarded[COMPANION_CLI_MAX_REPLY + 2]; memset(guarded, 0xA5, sizeof(guarded));
  uint8_t frame[] = {COMPANION_CMD_RUN_CLI_COMMAND, '0', '1', '|', 'x'};
  const size_t n = handleCompanionBatteryCLIFrame(board, prefs, frame, sizeof(frame),
    guarded + 1, COMPANION_CLI_MAX_REPLY, []() {});
  EXPECT_EQ(0xA5, guarded[0]); EXPECT_EQ(0xA5, guarded[sizeof(guarded) - 1]);
  EXPECT_EQ("01|Unknown command", std::string(reinterpret_cast<char*>(guarded + 2), n - 1));
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
