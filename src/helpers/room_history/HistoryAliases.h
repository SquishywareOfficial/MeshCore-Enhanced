#pragma once
#include "HistoryStorage.h"
#include "HistoryCodec.h"
#include "HistoryMembers.h"
#include <ctype.h>
namespace room_history {
struct Alias { uint8_t key[32] = {}; char name[32] = {}; };
constexpr size_t AliasBudget = 2 * (HeaderBytes + MemberCapacity * sizeof(Alias));
// Independent, room-bound, alternating checked snapshots. Names are admin aliases,
// never phone names or an authorization source. Public-key commands work even if
// this optional table requires recovery.
class HistoryAliases {
  Storage& storage;
  uint8_t room[32] = {};
  Alias* entries = nullptr;
  uint16_t used = 0;
  uint64_t revision = 0;
  State state_ = State::Unopened;
  Result validSnapshot(const char*, uint64_t&, uint16_t&);
public:
  explicit HistoryAliases(Storage& s) : storage(s) {}
  ~HistoryAliases() { storage.release(entries); }
  HistoryAliases(const HistoryAliases&) = delete;
  HistoryAliases& operator=(const HistoryAliases&) = delete;
  Result begin(const uint8_t key[32]);
  Result assign(const uint8_t key[32], const char* name); // '-' removes
  Result select(const char* token, HistoryMembers&, uint8_t key[32]);
  const char* name(const uint8_t key[32]) const;
  State state() const { return state_; }
  uint16_t count() const { return state_ == State::Ready ? used : 0; }
  const Alias& at(uint16_t i) const { return entries[i]; }
  static bool same(const char* a, const char* b);
  static bool validName(const char*);
};
}
