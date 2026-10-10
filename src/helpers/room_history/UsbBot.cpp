#include "UsbBot.h"
#include <stdio.h>

namespace room_history {
static void hexEncode(const uint8_t* input, size_t size, char* output) {
  const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i) { output[i * 2] = digits[input[i] >> 4]; output[i * 2 + 1] = digits[input[i] & 15]; }
  output[size * 2] = 0;
}
bool UsbBot::emit(Stream& out, const char* text) {
  // One bounded write prevents unbounded byte-at-a-time output loops.
  char line[640]; int n = snprintf(line, sizeof(line), "@bot %s\n", text);
  return n > 0 && size_t(n) < sizeof(line) && out.write((const uint8_t*)line, size_t(n)) == size_t(n);
}
bool UsbBot::failure(Stream& out, const char* reason) {
  char reply[96]; snprintf(reply, sizeof(reply), "{\"type\":\"error\",\"error\":\"%s\"}", reason); return emit(out, reply);
}
bool UsbBot::info(Stream& out) {
  char key[65], reply[384]; hexEncode(roomKey, 32, key);
  snprintf(reply, sizeof(reply), "{\"type\":\"info\",\"api\":1,\"room_key\":\"%s\",\"count\":%u,\"oldest\":\"%llu\",\"latest\":\"%llu\",\"high_water\":\"%llu\",\"max_page\":8,\"max_text_bytes\":151}",
    key, unsigned(history.count()), (unsigned long long)(history.count() ? history.at(0).sequence : 0),
    (unsigned long long)(history.count() ? history.at(history.count() - 1).sequence : 0), (unsigned long long)history.highWater());
  return emit(out, reply);
}
bool UsbBot::read(Stream& out, const char* args) {
  const char* separator = strchr(args, ' '); char cursor[21]; uint64_t after = 0, limit = 4;
  size_t n = separator ? size_t(separator - args) : strlen(args);
  if (!n || n >= sizeof(cursor)) return failure(out, "invalid cursor");
  memcpy(cursor, args, n); cursor[n] = 0;
  if (!parseUnsigned(cursor, UINT64_MAX, after) || (separator && (!parseUnsigned(separator + 1, MaxPage, limit) || !limit)))
    return failure(out, "invalid cursor or limit");
  if (after > history.highWater()) return failure(out, "cursor ahead of archive");
  uint64_t oldest = history.count() ? history.at(0).sequence : 0;
  uint64_t latest = history.count() ? history.at(history.count() - 1).sequence : 0;
  bool gap = oldest && after < oldest - 1; uint64_t next = after; unsigned sent = 0;
  for (uint16_t i = 0; i < history.count() && sent < limit; ++i) {
    const auto& entry = history.at(i); if (entry.sequence <= after) continue;
    Post p; Result result = history.read(entry, p);
    if (result != Result::Ok) return failure(out, error(result));
    char key[65], text[TextBytes * 2 + 1], reply[560];
    hexEncode(p.author, 32, key); hexEncode((const uint8_t*)p.text, p.length, text);
    snprintf(reply, sizeof(reply), "{\"type\":\"post\",\"sequence\":\"%llu\",\"timestamp\":%lu,\"sender_timestamp\":%lu,\"author\":\"%s\",\"kind\":%u,\"text_hex\":\"%s\"}",
      (unsigned long long)p.sequence, (unsigned long)p.timestamp, (unsigned long)p.senderTimestamp, key, unsigned(p.kind), text);
    if (!emit(out, reply)) return false;
    next = p.sequence; ++sent;
  }
  char reply[256];
  snprintf(reply, sizeof(reply), "{\"type\":\"end\",\"after\":\"%llu\",\"next\":\"%llu\",\"oldest\":\"%llu\",\"latest\":\"%llu\",\"high_water\":\"%llu\",\"count\":%u,\"gap\":%s,\"more\":%s}",
    (unsigned long long)after, (unsigned long long)next, (unsigned long long)oldest, (unsigned long long)latest,
    (unsigned long long)history.highWater(), sent, gap ? "true" : "false", latest > next ? "true" : "false");
  return emit(out, reply);
}
}
