#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Existing companion eviction policy, with observable acceptance. Tests exercise
// this same function at 16 and 256 entries, rather than a copied queue model.
template<class Frame> bool enqueueOfflineMessage(Frame* queue, int& count, int capacity, const uint8_t* bytes, int length) {
  if (length < 1 || size_t(length) > sizeof(queue[0].buf) || count < 0 || count > capacity) return false;
  if (count == capacity) {
    int victim = 0; while (victim < count && !queue[victim].isChannelMsg()) ++victim;
    if (victim == count) return false;
    for (int i = victim; i + 1 < count; ++i) queue[i] = queue[i + 1];
    --count;
  }
  queue[count].len = length; memcpy(queue[count].buf, bytes, length); ++count; return true;
}
inline int renderContactMessage(uint8_t* frame, size_t capacity, bool v3, float snr, const uint8_t* key,
  uint8_t path, uint8_t type, uint32_t timestamp, const uint8_t* extra, size_t extraLength, const char* text) {
  size_t header = (v3 ? 4 : 1) + 6 + 1 + 1 + 4 + extraLength;
  if (!text || header > capacity || (extraLength && !extra)) return 0;
  size_t i = 0;
  if (v3) { frame[i++] = 16; frame[i++] = int8_t(snr * 4); frame[i++] = 0; frame[i++] = 0; }
  else frame[i++] = 7;
  memcpy(frame + i, key, 6); i += 6; frame[i++] = path; frame[i++] = type;
  memcpy(frame + i, &timestamp, 4); i += 4;
  if (extraLength) { memcpy(frame + i, extra, extraLength); i += extraLength; }
  size_t n = strlen(text); if (n > capacity - i) n = capacity - i;
  memcpy(frame + i, text, n); return int(i + n);
}
