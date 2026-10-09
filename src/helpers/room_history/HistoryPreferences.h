#pragma once
#include <Arduino.h>
#include "HistoryStorage.h"
#include <stdio.h>
namespace room_history {
inline bool preferenceEnvelope(const uint8_t* data, size_t length) {
  if (!length || memchr(data, 0, length)) return false;
  size_t first = 0, last = length;
  auto space = [](uint8_t c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; };
  while (first < last && space(data[first])) ++first;
  while (last > first && space(data[last - 1])) --last;
  return last > first && data[first] == '{' && data[last - 1] == '}';
}
class PreferenceBuffer : public Stream {
public:
  static constexpr size_t Limit = 4096;
  uint8_t data[Limit]; size_t length = 0, position = 0; bool overflow = false;
  int available() override { return int(length - position); }
  int read() override { return position < length ? data[position++] : -1; }
  int peek() override { return position < length ? data[position] : -1; }
  size_t write(uint8_t n) override {
    if (length == Limit) { overflow = true; return 0; }
    data[length++] = n; return 1;
  }
  using Print::write;
  void flush() override {}
};
// Keep the previous whole preference file until the replacement is checked.
// Boot recovery runs before CommonCLI load, including cuts between renames.
template<class Prefs> bool savePreferences(Storage& storage, Prefs& prefs, PreferenceBuffer& b) {
  b.length = b.position = 0; b.overflow = false;
  if (!prefs.saveSerial(b) || b.overflow || !b.length || !headroom(storage, b.length)) return false;
  if (!storage.write("/prefs.pending", b.data, b.length, false)) return false;
  uint8_t readback[256];
  for (size_t offset = 0; offset < b.length; offset += sizeof(readback)) {
    size_t n = b.length - offset; if (n > sizeof(readback)) n = sizeof(readback);
    if (!storage.read("/prefs.pending", offset, readback, n) || memcmp(readback, b.data + offset, n)) return false;
  }
  FileInfo files[4]; size_t count;
  if (!storage.list("/prefs.", files, 4, count)) return false;
  bool current = false, previous = false;
  for (size_t i = 0; i < count; ++i) {
    current |= strcmp(files[i].name, "/prefs.json") == 0;
    previous |= strcmp(files[i].name, "/prefs.previous") == 0;
  }
  if (previous && !current) return false;
  if (previous && !storage.remove("/prefs.previous")) return false;
  if (current && !storage.rename("/prefs.json", "/prefs.previous")) return false;
  if (!storage.rename("/prefs.pending", "/prefs.json")) {
    if (current) storage.rename("/prefs.previous", "/prefs.json");
    return false;
  }
  return true;
}
}
