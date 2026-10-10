#pragma once
#include <Arduino.h>
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && ARDUINO_USB_CDC_ON_BOOT
#include <hal/usb_serial_jtag_ll.h>
#endif

namespace room_history {
// Arduino 2.x HWCDC drains its software ring but omits the final USB ZLP.
// Complete each bounded reply before emitting another; leave the JSON unchanged.
// See Espressif esp32s3 usb_serial_jtag_ll_txfifo_flush documentation: a full
// 64-byte packet needs another flush once txfifo_writable() becomes true.
class UsbReplyStream : public Stream {
public:
  int available() override { return Serial.available(); }
  int read() override { return Serial.read(); }
  int peek() override { return Serial.peek(); }
  void flush() override {
    Serial.flush();
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 1 && ARDUINO_USB_CDC_ON_BOOT
    uint32_t started = millis();
    while (!usb_serial_jtag_ll_txfifo_writable() && uint32_t(millis() - started) < 100) delay(1);
    if (usb_serial_jtag_ll_txfifo_writable()) usb_serial_jtag_ll_txfifo_flush();
    // Let the host acknowledge the terminating packet and the legacy driver's
    // IN_EMPTY handler settle before the next producer write (two USB frames).
    delay(2);
#endif
  }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    size_t written = Serial.write(data, size);
    flush();
    return written;
  }
};
} // namespace room_history
