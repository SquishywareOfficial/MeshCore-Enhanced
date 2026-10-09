#pragma once

#include <cstdint>
#include <cmath>
#include <stdio.h>
#include "Stream.h"

inline uint32_t g_mock_millis = 0;

using std::isnan;

inline uint32_t millis() {
  return g_mock_millis;
}

inline void delay(uint32_t ms) {
  g_mock_millis += ms;
}

#if !defined(_WIN32)
// Arduino supplies ltoa; Linux host tests need this non-standard API shim.
inline char* ltoa(long value, char* buffer, int radix) {
  if (radix < 2 || radix > 36) { buffer[0] = 0; return buffer; }
  const bool negative = value < 0 && radix == 10;
  unsigned long magnitude = negative ? 0ul - static_cast<unsigned long>(value) : static_cast<unsigned long>(value);
  char digits[sizeof(long) * 8]; size_t count = 0;
  do {
    digits[count++] = "0123456789abcdefghijklmnopqrstuvwxyz"[magnitude % radix];
    magnitude /= radix;
  } while (magnitude);
  char* out = buffer;
  if (negative) *out++ = '-';
  while (count) *out++ = digits[--count];
  *out = 0;
  return buffer;
}
#endif
