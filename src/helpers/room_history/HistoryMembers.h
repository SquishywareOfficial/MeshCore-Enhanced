#pragma once
#include "HistoryStorage.h"
#include "HistoryCodec.h"
namespace room_history {
constexpr size_t MemberRecordBytes = 128, MemberLogBudget = 48 * 1024;
struct PurgeCounts { uint16_t removed = 0, unknown = 0, future = 0; };
// Two bounded snapshots plus a bounded mutation log. A complete newer snapshot
// is the generation switch: no erase of the last committed generation first.
class HistoryMembers {
  Storage& storage;
  uint8_t server[32] = {};
  Member* table = nullptr;
  uint16_t used = 0;
  uint64_t revision_ = 0;
  uint32_t epoch_ = 1, generation = 0;
  size_t logBytes = HeaderBytes;
  State state_ = State::Unopened;
  Result fail(Result);
  Result compact(const uint8_t* removed, uint32_t epoch);
  Result mutate(uint8_t operation, const Member&);
  Result apply(uint8_t operation, const Member&);
  bool cleanup();
  static void name(char*, bool snapshot, uint32_t generation);
public:
  uint32_t failures = 0;
  explicit HistoryMembers(Storage& s) : storage(s) {}
  ~HistoryMembers() { storage.release(table); }
  HistoryMembers(const HistoryMembers&) = delete;
  HistoryMembers& operator=(const HistoryMembers&) = delete;
  Result begin(const uint8_t key[32]);
  Member* find(const uint8_t key[32]);
  const Member& at(uint16_t index) const { return table[index]; }
  uint16_t count() const { return used; }
  uint64_t revision() const { return revision_; }
  State state() const { return state_; }
  size_t memoryBytes() const { return table ? sizeof(Member) * MemberCapacity : 0; }
  bool bytes(size_t&);
  Result login(const uint8_t key[32], uint64_t floor, bool known, uint64_t utc, Member*& result);
  Result delivered(const uint8_t key[32], uint64_t incarnation, uint64_t sequence, uint32_t timestamp);
  Result select(const char* prefix, uint8_t key[32]);
  Result forget(const char* prefix, PurgeCounts&);
  Result forgetInactive(uint64_t days, bool trusted, uint64_t now, PurgeCounts&);
};
}
