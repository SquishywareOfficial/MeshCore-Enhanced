#pragma once
#include <stdint.h>
namespace mesh {
enum class OutputOperation { Configure, On, Off, Remove };
enum class OutputResult { Success, Unsupported, InvalidGpio, Reserved, NotConfigured };
// Stable persistent bit indexes, NOT shifts by the chip GPIO number.
inline int outputGpioAt(int index) {
  const int pins[] = {1, 2, 4, 43, 44};
  return index >= 0 && index < 5 ? pins[index] : -1;
}
inline uint8_t outputBitForGpio(int gpio) {
  for (int i = 0; i < 5; ++i) if (outputGpioAt(i) == gpio) return uint8_t(1u << i);
  return 0;
}
}
