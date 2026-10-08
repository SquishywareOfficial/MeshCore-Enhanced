#include <gtest/gtest.h>
#include <MeshCore.h>

// The hardware ESP32Board is excluded in native builds. Supply its small board
// interface and ADC calls so these tests exercise the actual XIAO implementation.
class ESP32Board : public mesh::MainBoard {
public:
    uint16_t getBattMilliVolts() override { return 0; }
    const char* getManufacturerName() const override { return "mock"; }
    void reboot() override { }
    uint8_t getStartupReason() const override { return 0; }
};

#define D0 1
#define D1 2
#define D3 4
#define BATTERY_ADC_PIN D0
#define INPUT 1
#define ADC_11db 3
static int pin_calls, adc_reads, attached_pin, attenuation_pin, attenuation, resolution;
static uint32_t adc_mv;
static int expected_pin;
static bool attach_ok;
void pinMode(int pin, int mode) { ++pin_calls; EXPECT_EQ(expected_pin, pin); EXPECT_EQ(INPUT, mode); }
bool adcAttachPin(int pin) { attached_pin = pin; return attach_ok; }
void analogSetPinAttenuation(int pin, int value) { attenuation_pin = pin; attenuation = value; }
void analogReadResolution(int bits) { resolution = bits; }
uint32_t analogReadMilliVolts(int pin) { ++adc_reads; EXPECT_EQ(expected_pin, pin); return adc_mv; }

#include "../../variants/xiao_s3_wio/XiaoS3WIOBoard.h"

class XiaoBattery : public ::testing::Test {
protected:
    XiaoS3WIOBoard board;
    void SetUp() override {
        pin_calls = adc_reads = attached_pin = attenuation_pin = attenuation = resolution = 0;
        adc_mv = 2100;
        expected_pin = D0;
        attach_ok = true;
    }
};

TEST_F(XiaoBattery, DefaultOffDoesNotTouchPinOrReadADC) {
    EXPECT_EQ(0, board.getBatteryConnected());
    EXPECT_EQ(0, board.getBattMilliVolts());
    EXPECT_TRUE(board.setBatteryConnected(false));
    EXPECT_EQ(0, adc_reads);
    EXPECT_EQ(0, pin_calls);
    EXPECT_EQ(0, attached_pin);
}

TEST_F(XiaoBattery, EnableConfiguresPinAndAppliesDividerOnce) {
    ASSERT_TRUE(board.setBatteryConnected(true));
    EXPECT_EQ(1, board.getBatteryConnected());
    EXPECT_EQ(D0, attached_pin);
    EXPECT_EQ(D0, attenuation_pin);
    EXPECT_EQ(ADC_11db, attenuation);
    EXPECT_EQ(4200, board.getBattMilliVolts());
    EXPECT_EQ(4, adc_reads);
    EXPECT_EQ(12, resolution);
    EXPECT_TRUE(board.setBatteryConnected(true));
    EXPECT_EQ(1, pin_calls);
}

TEST_F(XiaoBattery, DisableStopsSamplingAndKeepsCalibration) {
    ASSERT_TRUE(board.setBatteryConnected(true));
    ASSERT_TRUE(board.setAdcMultiplier(2.05f));
    adc_mv = 2000;
    EXPECT_EQ(4100, board.getBattMilliVolts());
    ASSERT_TRUE(board.setBatteryConnected(false));
    adc_reads = 0;
    EXPECT_EQ(0, board.getBattMilliVolts());
    EXPECT_EQ(0, adc_reads);
    EXPECT_FLOAT_EQ(2.05f, board.getAdcMultiplier());
    ASSERT_TRUE(board.setBatteryConnected(true));
    EXPECT_EQ(4100, board.getBattMilliVolts());
    ASSERT_TRUE(board.setAdcMultiplier(0));
    EXPECT_EQ(4000, board.getBattMilliVolts());
}

TEST_F(XiaoBattery, InvalidCalibrationDoesNotChangeMultiplier) {
    EXPECT_FALSE(board.setAdcMultiplier(-1));
    EXPECT_FALSE(board.setAdcMultiplier(11));
    EXPECT_FALSE(board.setAdcMultiplier(NAN));
    EXPECT_FALSE(board.setAdcMultiplier(INFINITY));
    EXPECT_FLOAT_EQ(2.0f, board.getAdcMultiplier());
}

TEST_F(XiaoBattery, PinSelectionDoesNotTouchHardwareUntilEnabled) {
    EXPECT_EQ(D0, board.getBatteryGpio());
    for (int pin : {D1, D3}) {
        ASSERT_TRUE(board.setBatteryGpio(pin));
        EXPECT_EQ(pin, board.getBatteryGpio());
        EXPECT_EQ(0, pin_calls);
        EXPECT_EQ(0, adc_reads);
        expected_pin = pin;
        ASSERT_TRUE(board.setBatteryConnected(true));
        EXPECT_EQ(pin, attached_pin);
        EXPECT_EQ(pin, attenuation_pin);
        EXPECT_EQ(4200, board.getBattMilliVolts());
        EXPECT_FALSE(board.setBatteryGpio(D0));
        EXPECT_EQ(pin, board.getBatteryGpio());
        ASSERT_TRUE(board.setBatteryConnected(false));
        pin_calls = adc_reads = 0;
    }
    ASSERT_TRUE(board.setBatteryGpio(-1));
    EXPECT_EQ(D0, board.getBatteryGpio());
    EXPECT_EQ(0, pin_calls);
}

TEST_F(XiaoBattery, ReservedAndInvalidPinsAreRejectedWithoutChangingSelection) {
    ASSERT_TRUE(board.setBatteryGpio(D3));
    for (int pin : {-2, 0, 3, 5, 6, 7, 8, 9, 43, 127}) {
        EXPECT_FALSE(board.setBatteryGpio(pin));
        EXPECT_EQ(D3, board.getBatteryGpio());
    }
    EXPECT_EQ(0, pin_calls);
    EXPECT_EQ(0, adc_reads);
}

TEST_F(XiaoBattery, FailedAdcAttachmentLeavesSensingOff) {
    attach_ok = false;
    EXPECT_FALSE(board.setBatteryConnected(true));
    EXPECT_EQ(0, board.getBatteryConnected());
    EXPECT_EQ(0, board.getBattMilliVolts());
    EXPECT_EQ(0, adc_reads);
    EXPECT_EQ(0, attenuation_pin);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
