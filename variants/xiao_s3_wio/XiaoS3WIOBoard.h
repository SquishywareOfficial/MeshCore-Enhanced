#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

#ifdef BATTERY_ADC_PIN
  #ifndef ADC_MULTIPLIER
    #define ADC_MULTIPLIER 2.0f
  #endif
  // Exposed ADC1 pins that do not overlap the Wio radio, I2C or boot straps.
  static_assert(BATTERY_ADC_PIN == D0 || BATTERY_ADC_PIN == D1 || BATTERY_ADC_PIN == D3,
                "Xiao S3 WIO battery divider must use D0, D1 or D3");
  static_assert(ADC_MULTIPLIER > 0.0f && ADC_MULTIPLIER <= 10.0f,
                "ADC_MULTIPLIER must be greater than 0 and at most 10");
#endif

class XiaoS3WIOBoard : public ESP32Board {
#ifdef BATTERY_ADC_PIN
  float adc_mult = ADC_MULTIPLIER;
  bool battery_connected = false;
  int battery_gpio = BATTERY_ADC_PIN;
#endif

#ifdef XIAO_WIO_OUTPUTS
  uint8_t outputs_mask = 0;
  uint8_t outputs_on = 0;

  bool outputPinReserved(int pin) const {
#ifdef BATTERY_ADC_PIN
    if (pin == battery_gpio) return true;
#endif
#ifdef P_LORA_DIO_1
    if (pin == P_LORA_DIO_1) return true;
#endif
#ifdef P_LORA_NSS
    if (pin == P_LORA_NSS) return true;
#endif
#ifdef P_LORA_RESET
    if (pin == P_LORA_RESET) return true;
#endif
#ifdef P_LORA_BUSY
    if (pin == P_LORA_BUSY) return true;
#endif
#ifdef P_LORA_SCLK
    if (pin == P_LORA_SCLK) return true;
#endif
#ifdef P_LORA_MISO
    if (pin == P_LORA_MISO) return true;
#endif
#ifdef P_LORA_MOSI
    if (pin == P_LORA_MOSI) return true;
#endif
#ifdef SX126X_RXEN
    if (pin == SX126X_RXEN) return true;
#endif
#ifdef SX126X_TXEN
    if (pin == SX126X_TXEN) return true;
#endif
#ifdef PIN_BOARD_SDA
    if (pin == PIN_BOARD_SDA) return true;
#endif
#ifdef PIN_BOARD_SCL
    if (pin == PIN_BOARD_SCL) return true;
#endif
#ifdef ENV_PIN_SDA
    if (pin == ENV_PIN_SDA) return true;
#endif
#ifdef ENV_PIN_SCL
    if (pin == ENV_PIN_SCL) return true;
#endif
#ifdef PIN_USER_BTN
    if (pin == PIN_USER_BTN) return true;
#endif
#ifdef PIN_STATUS_LED
    if (pin == PIN_STATUS_LED) return true;
#endif
#ifdef SERIAL_TX
    if (pin == SERIAL_TX) return true;
#endif
#ifdef SERIAL_RX
    if (pin == SERIAL_RX) return true;
#endif
#ifdef WITH_RS232_BRIDGE_RX
    if (pin == WITH_RS232_BRIDGE_RX) return true;
#endif
#ifdef WITH_RS232_BRIDGE_TX
    if (pin == WITH_RS232_BRIDGE_TX) return true;
#endif
#ifdef PIN_GPS_RX
    if (pin == PIN_GPS_RX) return true;
#endif
#ifdef PIN_GPS_TX
    if (pin == PIN_GPS_TX) return true;
#endif
#ifdef PIN_GPS_EN
    if (pin == PIN_GPS_EN) return true;
#endif
#ifdef PIN_GPS_RESET
    if (pin == PIN_GPS_RESET) return true;
#endif
#ifdef GPS_EN
    if (pin == GPS_EN) return true;
#endif
#ifdef GPS_RESET
    if (pin == GPS_RESET) return true;
#endif
#if defined(ARDUINO_USB_CDC_ON_BOOT) && !ARDUINO_USB_CDC_ON_BOOT
    // Serial.begin uses UART0 when the USB CDC console is disabled.
    if (pin == 43 || pin == 44) return true;
#endif
    return false;
  }
#endif

public:
  XiaoS3WIOBoard() { }

#ifdef XIAO_WIO_OUTPUTS
  int getOutputMask() const override { return outputs_mask; }
  int getOutputState(int pin) const override {
    const auto bit = mesh::outputBitForGpio(pin);
    return (outputs_mask & bit) ? ((outputs_on & bit) ? 1 : 0) : -1;
  }
  mesh::OutputResult controlOutput(mesh::OutputOperation op, int pin, bool& changed) override {
    changed = false;
    const uint8_t bit = mesh::outputBitForGpio(pin);
    if (!bit) return mesh::OutputResult::InvalidGpio;
    // A repeated removal must never disturb a pin now owned by another device.
    if (op == mesh::OutputOperation::Remove && !(outputs_mask & bit))
      return mesh::OutputResult::Success;
    if (outputPinReserved(pin)) return mesh::OutputResult::Reserved;
    if (op == mesh::OutputOperation::Configure) {
      if (!(outputs_mask & bit)) {
        digitalWrite(pin, LOW); // initialise latch BEFORE enabling the driver
        pinMode(pin, OUTPUT);
        outputs_mask |= bit;
        outputs_on &= ~bit;
        changed = true;
      }
    } else {
      if (!(outputs_mask & bit)) return mesh::OutputResult::NotConfigured;
      if (op == mesh::OutputOperation::Remove) {
        digitalWrite(pin, LOW);
        pinMode(pin, INPUT);
        outputs_mask &= ~bit;
        outputs_on &= ~bit;
        changed = true;
      } else {
        const bool on = op == mesh::OutputOperation::On;
        if (on != bool(outputs_on & bit)) {
          digitalWrite(pin, on ? HIGH : LOW);
          if (on) outputs_on |= bit; else outputs_on &= ~bit;
        }
      }
    }
    return mesh::OutputResult::Success;
  }
  uint8_t restoreOutputs(uint8_t saved_mask) override {
    // Called once during startup, after battery ownership has been resolved.
    // Filter the entire saved assignment set before performing any GPIO access.
    uint8_t allowed_mask = saved_mask & 0x1f;
    for (int i = 0; i < 5; ++i) {
      if (outputPinReserved(mesh::outputGpioAt(i))) allowed_mask &= ~(1u << i);
    }
    outputs_mask = outputs_on = 0;
    for (int i = 0; i < 5; ++i) {
      if (!(allowed_mask & (1u << i))) continue;
      bool changed;
      controlOutput(mesh::OutputOperation::Configure, mesh::outputGpioAt(i), changed);
    }
    return outputs_mask;
  }
#endif

#ifdef BATTERY_ADC_PIN
  bool setBatteryGpio(int pin) override {
    if (battery_connected) return false; // disable sensing before selecting another pin
    if (pin == -1) pin = BATTERY_ADC_PIN;
    if (pin != D0 && pin != D1 && pin != D3) return false;
#ifdef XIAO_WIO_OUTPUTS
    if (outputs_mask & mesh::outputBitForGpio(pin)) return false;
#endif
    battery_gpio = pin;
    return true; // selection alone must not touch an unwired pin
  }

  int getBatteryGpio() const override { return battery_gpio; }

  bool setBatteryConnected(bool connected) override {
    if (connected == battery_connected) return true;
    // Leave the pin untouched until sensing is explicitly enabled. On disable,
    // release the ADC pin back to a plain input and stop sampling it.
    pinMode(battery_gpio, INPUT);
    if (connected) {
      if (!adcAttachPin(battery_gpio)) return false;
      analogSetPinAttenuation(battery_gpio, ADC_11db);
    }
    battery_connected = connected;
    return true;
  }

  int getBatteryConnected() const override { return battery_connected ? 1 : 0; }

  uint16_t getBattMilliVolts() override {
    if (!battery_connected) return 0; // no ADC access to an unwired/floating pin
    analogReadResolution(12);
    uint32_t millivolts = 0;
    for (int i = 0; i < 4; i++) {
      millivolts += analogReadMilliVolts(battery_gpio);
    }
    // Same calibrated four-sample ADC approach as ESP32Board, with one correction.
    return static_cast<uint16_t>((millivolts / 4.0f) * adc_mult);
  }

  bool setAdcMultiplier(float multiplier) override {
    // Also rejects NaN/Infinity before converting the scaled reading to uint16_t.
    if (!(multiplier >= 0.0f && multiplier <= 10.0f)) return false;
    adc_mult = multiplier == 0.0f ? ADC_MULTIPLIER : multiplier;
    return true;
  }

  float getAdcMultiplier() const override { return adc_mult; }
#endif

  const char* getManufacturerName() const override {
    return "Xiao S3 WIO";
  }
};
