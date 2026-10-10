#pragma once
#include "RoomHistory.h"
#include <Stream.h>
#include <stdio.h>

namespace room_history {
// Explicitly dispatched by the USB loop, never by the radio/admin CLI.
class UsbBot {
  RoomHistory& history;
  const uint8_t* roomKey;
  bool emit(Stream&, const char*);
  bool failure(Stream&, const char*);
  bool info(Stream&);
  bool read(Stream&, const char*);
public:
  static constexpr uint8_t ApiVersion = 1, MaxPage = 8;
  explicit UsbBot(RoomHistory& h, const uint8_t* key) : history(h), roomKey(key) {}
  static bool isCommand(const char* text) { return text && !strncmp(text, "bot.", 4); }
  // append must use kind=0 and the ROOM's full identity. Existing submission
  // deduplication retains request IDs without changing the disk/wire schemas.
  template<class Append> bool handle(const char* command, Stream& out, Append append) {
    if (!isCommand(command)) return false;
    if (history.state() != State::Ready) { failure(out, "recovery required"); return true; }
    if (!strcmp(command, "bot.info")) { info(out); return true; }
    if (!strncmp(command, "bot.read ", 9)) { read(out, command + 9); return true; }
    if (!strncmp(command, "bot.post ", 9)) {
      const char* separator = strchr(command + 9, ' ');
      char id[11]; uint64_t request = 0;
      if (!separator || separator == command + 9 || size_t(separator - (command + 9)) >= sizeof(id)) {
        failure(out, "invalid request"); return true;
      }
      memcpy(id, command + 9, separator - (command + 9)); id[separator - (command + 9)] = 0;
      const char* hex = separator + 1; size_t n = strlen(hex); char text[TextBytes + 1] = {};
      if (!parseUnsigned(id, UINT32_MAX, request) || !request || !n || n % 2 || n > TextBytes * 2) {
        failure(out, "invalid request"); return true;
      }
      for (size_t i = 0; i < n; i += 2) {
        int a = nibble(hex[i]), b = nibble(hex[i + 1]);
        if (a < 0 || b < 0 || !(a | b)) { failure(out, "invalid text"); return true; }
        text[i / 2] = char((a << 4) | b);
      }
      Post committed;
      Result result = append(uint32_t(request), text, committed);
      if (result != Result::Ok && result != Result::Duplicate) { failure(out, error(result)); return true; }
      char reply[160];
      snprintf(reply, sizeof(reply), "{\"type\":\"posted\",\"request_id\":\"%lu\",\"sequence\":\"%llu\",\"timestamp\":%lu,\"duplicate\":%s}",
        (unsigned long)request, (unsigned long long)committed.sequence, (unsigned long)committed.timestamp,
        result == Result::Duplicate ? "true" : "false");
      emit(out, reply); return true;
    }
    failure(out, "unknown command"); return true;
  }
  static int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
};

// A full overlong line is discarded until CR/LF; no truncated command executes.
class UsbBotLine {
  char buffer[384] = {};
  size_t length = 0;
  bool invalid = false;
public:
  enum Status { Incomplete, Ready, Rejected };
  Status feed(char c) {
    if (c == '\r' || c == '\n') {
      if (!length && !invalid) return Incomplete;
      buffer[length] = 0; return invalid ? Rejected : Ready;
    }
    if (!c || length == sizeof(buffer) - 1) invalid = true;
    if (!invalid) buffer[length++] = c;
    return Incomplete;
  }
  const char* text() const { return buffer; }
  void reset() { length = 0; invalid = false; buffer[0] = 0; }
};
}
