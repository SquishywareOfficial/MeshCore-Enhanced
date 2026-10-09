#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace room_history {
constexpr uint16_t Capacity = 2000, DefaultPlayback = 100, MemberCapacity = 256;
constexpr size_t ArchiveBudget = 512 * 1024, MemberBudget = 128 * 1024;
constexpr size_t RecordBytes = 224, HeaderBytes = 64, TextBytes = 151;
constexpr uint16_t SegmentSlots = 64, MaxSegments = 34;
enum class Result : uint8_t { Ok, Duplicate, Invalid, Conflict, Io, Full, Recovery, NotFound, Ambiguous, ClockUnset };
enum class State : uint8_t { Unopened, Ready, Recovery };
struct Post {
  uint64_t sequence = 0;
  uint32_t timestamp = 0, senderTimestamp = 0;
  uint8_t author[32] = {}, kind = 0;
  uint16_t length = 0;
  char text[TextBytes + 1] = {};
};
struct IndexEntry {
  uint64_t sequence;
  uint32_t timestamp, senderTimestamp, generation, authorHash, contentHash;
  uint16_t slot;
  uint8_t kind, reserved;
  uint32_t readyAt;
};
static_assert(sizeof(IndexEntry) <= 40, "History index budget");
struct Member {
  uint8_t key[32] = {};
  uint64_t incarnation = 0, joinFloor = 0, delivered = 0, lastLogin = 0;
  uint32_t deliveredTimestamp = 0;
  bool loginKnown = false;
};
static_assert(sizeof(Member) * MemberCapacity <= 32 * 1024, "Member RAM budget");
inline const char* error(Result r) {
  switch (r) {
    case Result::Ok: return "OK";
    case Result::Duplicate: return "duplicate";
    case Result::Invalid: return "invalid value";
    case Result::Conflict: return "submission conflict";
    case Result::Io: return "storage failure";
    case Result::Full: return "storage full";
    case Result::Recovery: return "recovery required";
    case Result::NotFound: return "not found";
    case Result::Ambiguous: return "ambiguous key";
    case Result::ClockUnset: return "clock not set";
  }
  return "unknown";
}
inline bool parseUnsigned(const char* s, uint64_t maximum, uint64_t& value) {
  if (!s || !*s) return false;
  uint64_t n = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    unsigned digit = *s - '0';
    if (digit > maximum || n > (maximum - digit) / 10) return false;
    n = n * 10 + digit;
  }
  value = n;
  return true;
}
inline bool parsePlayback(const char* text, uint16_t& value) {
  if (text && strcmp(text, "default") == 0) { value = DefaultPlayback; return true; }
  uint64_t parsed;
  if (!parseUnsigned(text, Capacity, parsed) || !parsed) return false;
  value = uint16_t(parsed); return true;
}
template<class Save> Result changePlayback(uint16_t& current, const char* input, Save save) {
  uint16_t next;
  if (!parsePlayback(input, next)) return Result::Invalid;
  uint16_t previous = current; current = next;
  if (!save()) { current = previous; return Result::Io; }
  return Result::Ok;
}
}
