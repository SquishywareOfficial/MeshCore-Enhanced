#pragma once
#include "HistoryStorage.h"
#include "HistoryCodec.h"
namespace room_history {
class RoomHistory {
  Storage& storage;
  uint8_t server[32] = {};
  IndexEntry* entries = nullptr;
  uint16_t start = 0, used = 0, slots = SegmentSlots;
  uint32_t generation = 0;
  Control control;
  State status = State::Unopened;
  void insert(const Post&, uint32_t segment, uint16_t slot);
  Result failed(Result);
  Result reserve(uint64_t, uint32_t);
  Result prune();
public:
  uint32_t failures = 0, damagedTails = 0;
  explicit RoomHistory(Storage& s) : storage(s) {}
  ~RoomHistory() { storage.release(entries); }
  RoomHistory(const RoomHistory&) = delete; RoomHistory& operator=(const RoomHistory&) = delete;
  Result begin(const uint8_t key[32]);
  Result append(const uint8_t author[32], uint32_t senderTimestamp, const char* text, uint8_t kind, uint32_t now, Post& committed, uint32_t monotonicMillis = 0);
  Result findSubmission(const uint8_t author[32], uint32_t senderTimestamp, const char* text, Post& found);
  Result read(const IndexEntry&, Post&);
  const IndexEntry& at(uint16_t offset) const { return entries[(start + offset) % Capacity]; }
  uint16_t indexSlot(uint16_t offset) const { return (start + offset) % Capacity; }
  uint16_t count() const { return used; }
  State state() const { return status; }
  void requireRecovery() { failed(Result::Recovery); }
  uint64_t highWater() const { return control.sequence; }
  uint32_t timestampFloor() const { return control.timestamp; }
  size_t indexBytes() const { return entries ? sizeof(IndexEntry) * Capacity : 0; }
  bool bytes(size_t&);
  void tick(uint32_t now) {
    for (uint16_t i = 0; i < used; ++i) {
      auto& e = entries[(start + i) % Capacity];
      if (e.reserved && int32_t(now - e.readyAt) >= 0) e.reserved = 0;
    }
  }
  static void segmentName(uint32_t, char out[40]);
};
}
