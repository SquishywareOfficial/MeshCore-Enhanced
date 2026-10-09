#pragma once
#include "RoomHistory.h"
#include "HistoryMembers.h"
namespace room_history {
struct PlaybackSession {
  uint8_t key[32] = {}, own[Capacity / 8] = {};
  uint64_t incarnation = 0, generation = 0, joinFloor = 0, cursor = 0, capFloor = 0, snapshot = 0, indexedThrough = 0;
  uint64_t pendingSequence = 0, pendingGeneration = 0;
  uint32_t hintTimestamp = 0, pendingTimestamp = 0, ack = 0;
  bool active = false;
};
class HistoryPlayback {
  RoomHistory& history;
  HistoryMembers& members;
  PlaybackSession sessions[20];
  uint64_t nextGeneration = 0;
  Result refresh(PlaybackSession&);
  bool eligible(const PlaybackSession&, uint16_t index) const;
public:
  uint32_t expired = 0;
  HistoryPlayback(RoomHistory& h, HistoryMembers& m) : history(h), members(m) {}
  PlaybackSession* session(const uint8_t key[32]);
  Result login(const Member&, uint32_t since, uint16_t limit);
  void reconcile();
  template<class Active> void retainSessions(Active active) {
    for (auto& s : sessions) if (s.active && !active(s.key)) s.active = false;
  }
  void remove(const uint8_t key[32]);
  void keepAlive(const uint8_t key[32], uint32_t since);
  Result next(const uint8_t key[32], Post&, uint32_t monotonicMillis);
  uint16_t count(const uint8_t key[32]);
  static uint8_t wireCount(uint16_t n) { return n > 255 ? 255 : uint8_t(n); }
  void pending(const uint8_t key[32], const Post&, uint32_t ack);
  void timeout(const uint8_t key[32]);
  Result acknowledge(const uint8_t key[32], uint32_t ack);
};
}
