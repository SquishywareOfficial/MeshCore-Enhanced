#include <gtest/gtest.h>
#include <MeshCore.h>
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
#define INPUT 1
#define OUTPUT 2
#define LOW 0
#define HIGH 1
#define ADC_11db 3
// Deliberately relocate active peripherals onto otherwise allowed GPIOs.
#define P_LORA_NSS 2
#define ENV_PIN_SDA 4
#define PIN_GPS_TX 43
#define WITH_RS232_BRIDGE_RX 44
static int touches = 0;
void pinMode(int,int) { ++touches; }
void digitalWrite(int,int) { ++touches; }
bool adcAttachPin(int) { ++touches; return true; }
void analogSetPinAttenuation(int,int) { ++touches; }
void analogReadResolution(int) { ++touches; }
uint32_t analogReadMilliVolts(int) { ++touches; return 2100; }
#include "../../variants/xiao_s3_wio/XiaoS3WIOBoard.h"
TEST(XiaoPinClaims, ActiveRadioI2CGPSAndBridgeClaimsWin) {
  XiaoS3WIOBoard board; bool changed = true;
  for (int gpio : {1,2,4,43,44}) {
    EXPECT_EQ(mesh::OutputResult::Reserved,
      board.controlOutput(mesh::OutputOperation::Configure, gpio, changed));
    EXPECT_FALSE(changed);
    EXPECT_EQ(mesh::OutputResult::Reserved,
      board.controlOutput(mesh::OutputOperation::On, gpio, changed));
  }
  EXPECT_EQ(0, board.restoreOutputs(255)); EXPECT_EQ(0, touches);
}
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
