#include "HistoryPlayback.h"
#include <algorithm>
namespace room_history {
PlaybackSession* HistoryPlayback::session(const uint8_t* key) {
  for (auto& s : sessions) if (s.active && !memcmp(s.key, key, 32)) {
    Member* m = members.find(key);
    if (!m || m->incarnation != s.incarnation) { s.active = false; return nullptr; }
    return &s;
  }
  return nullptr;
}
void HistoryPlayback::remove(const uint8_t* key) { for (auto& s : sessions) if (s.active && !memcmp(s.key, key, 32)) s.active = false; }
void HistoryPlayback::reconcile() { for (auto& s : sessions) if (s.active) session(s.key); }
Result HistoryPlayback::refresh(PlaybackSession& s) {
  uint32_t hash = crc32(s.key, 32);
  for (uint16_t i = 0; i < history.count(); ++i) {
    const auto& e = history.at(i); if (e.sequence <= s.indexedThrough) continue;
    uint16_t slot = history.indexSlot(i); s.own[slot / 8] &= ~(1u << (slot % 8));
    if (e.authorHash == hash) {
      Post p; Result r = history.read(e, p); if (r != Result::Ok) return r;
      if (!memcmp(p.author, s.key, 32)) s.own[slot / 8] |= 1u << (slot % 8);
    }
  }
  if (history.count()) s.indexedThrough = history.at(history.count() - 1).sequence;
  return Result::Ok;
}
bool HistoryPlayback::eligible(const PlaybackSession& s, uint16_t i) const {
  const auto& e = history.at(i); uint16_t slot = history.indexSlot(i);
  return e.sequence > s.joinFloor && e.sequence > s.cursor && e.timestamp > s.hintTimestamp &&
         (e.sequence > s.snapshot || e.sequence > s.capFloor) && !(s.own[slot / 8] & (1u << (slot % 8)));
}
Result HistoryPlayback::login(const Member& m, uint32_t since, uint16_t limit) {
  if (history.state() != State::Ready || members.state() != State::Ready) return Result::Recovery;
  if (!limit || limit > Capacity || nextGeneration == UINT64_MAX) return Result::Invalid;
  PlaybackSession* s = session(m.key);
  if (!s) for (auto& candidate : sessions) if (!candidate.active) { s = &candidate; break; }
  if (!s) return Result::Full;
  *s = PlaybackSession{}; memcpy(s->key, m.key, 32); s->active = true; s->incarnation = m.incarnation;
  s->generation = ++nextGeneration; s->joinFloor = m.joinFloor; s->cursor = m.delivered;
  s->hintTimestamp = m.deliveredTimestamp; s->snapshot = history.highWater();
  if (since <= history.timestampFloor()) s->hintTimestamp = std::max(since, s->hintTimestamp);
  Result r = refresh(*s); if (r != Result::Ok) { s->active = false; return r; }
  uint16_t selected = 0;
  for (uint16_t i = history.count(); i > 0; --i) if (eligible(*s, i - 1) && ++selected == limit) {
    s->capFloor = history.at(i - 1).sequence - 1; break;
  }
  return Result::Ok;
}
uint16_t HistoryPlayback::replayCount(const PlaybackSession& s) const {
  if (!s.replayActive) return 0;
  uint16_t count = 0;
  for (uint16_t i = 0; i < history.count(); ++i) {
    uint64_t seq = history.at(i).sequence;
    if (seq >= s.replayFirst && seq <= s.replayLast && seq > s.replayCursor) ++count;
  }
  return count;
}
Result HistoryPlayback::replay(const uint8_t* key, uint16_t startOffset, uint16_t endOffset) {
  if (history.state() != State::Ready || members.state() != State::Ready) return Result::Recovery;
  PlaybackSession* s = session(key); if (!s) return Result::NotFound;
  if (s->ack || s->replayActive) return Result::Busy;
  if (startOffset < endOffset || startOffset >= history.count() || nextGeneration == UINT64_MAX) return Result::Invalid;
  s->generation = ++nextGeneration; s->pendingSequence = 0; s->pendingReplay = false;
  s->replayFirst = history.at(history.count() - 1 - startOffset).sequence;
  s->replayLast = history.at(history.count() - 1 - endOffset).sequence;
  s->replayCursor = s->replayFirst - 1; s->replayRemaining = startOffset - endOffset + 1;
  s->replayExpired = 0; s->replayActive = true; return Result::Ok;
}
Result HistoryPlayback::cancelReplay(const uint8_t* key) {
  PlaybackSession* s = session(key); if (!s) return Result::NotFound;
  if (s->replayActive || s->pendingReplay) {
    if (nextGeneration == UINT64_MAX) return Result::Full;
    s->generation = ++nextGeneration; s->replayActive = s->pendingReplay = false;
    s->ack = 0; s->pendingSequence = 0; s->replayRemaining = 0;
  }
  return Result::Ok;
}
void HistoryPlayback::keepAlive(const uint8_t* key, uint32_t since) {
  PlaybackSession* s = session(key); if (!s || since > history.timestampFloor()) return;
  // Hints never clear a pending ACK, persist delivery, or rewind the cap/window.
  s->hintTimestamp = std::max(since, s->hintTimestamp);
}
Result HistoryPlayback::next(const uint8_t* key, Post& p, uint32_t monotonicMillis) {
  if (history.state() != State::Ready || members.state() != State::Ready) return Result::Recovery;
  PlaybackSession* s = session(key); if (!s) return Result::NotFound;
  Result r = refresh(*s); if (r != Result::Ok) return r;
  if (s->replayActive) {
    uint16_t available = replayCount(*s);
    if (available < s->replayRemaining) { s->replayExpired += s->replayRemaining - available; s->replayRemaining = available; }
    for (uint16_t i = 0; i < history.count(); ++i) {
      const auto& e = history.at(i);
      if (e.sequence < s->replayFirst || e.sequence > s->replayLast || e.sequence <= s->replayCursor) continue;
      if (e.reserved && int32_t(monotonicMillis - e.readyAt) < 0) return Result::NotFound;
      if (s->pendingSequence && s->pendingSequence != e.sequence) { ++expired; s->pendingSequence = 0; s->ack = 0; }
      return history.read(e, p);
    }
    s->replayActive = false; s->pendingReplay = false;
    s->ack = 0; s->pendingSequence = 0;
  }
  for (uint16_t i = 0; i < history.count(); ++i) if (eligible(*s, i)) {
    const auto& e = history.at(i);
    if (e.reserved && int32_t(monotonicMillis - e.readyAt) < 0) return Result::NotFound;
    if (s->pendingSequence && s->pendingSequence < e.sequence) { ++expired; s->pendingSequence = 0; s->ack = 0; }
    return history.read(e, p);
  }
  return Result::NotFound;
}
uint16_t HistoryPlayback::count(const uint8_t* key) {
  if (history.state() != State::Ready || members.state() != State::Ready) return 0;
  PlaybackSession* s = session(key); if (!s || refresh(*s) != Result::Ok) return 0;
  uint16_t n = replayCount(*s); for (uint16_t i = 0; i < history.count(); ++i) if (eligible(*s, i)) ++n; return n;
}
void HistoryPlayback::pending(const uint8_t* key, const Post& p, uint32_t ack) {
  PlaybackSession* s = session(key); if (!s) return;
  s->pendingReplay = s->replayActive;
  s->pendingSequence = p.sequence; s->pendingTimestamp = p.timestamp; s->pendingGeneration = s->generation; s->ack = ack;
}
void HistoryPlayback::timeout(const uint8_t* key) { PlaybackSession* s = session(key); if (s) s->ack = 0; }
Result HistoryPlayback::acknowledge(const uint8_t* key, uint32_t ack) {
  PlaybackSession* s = session(key);
  if (!s || !s->ack || s->ack != ack || s->pendingGeneration != s->generation) return Result::NotFound;
  if (s->pendingReplay) {
    // Historical delivery never mutates the durable normal watermark or hints.
    s->replayCursor = s->pendingSequence;
    if (s->replayRemaining) --s->replayRemaining;
    if (s->replayCursor >= s->replayLast) s->replayActive = false;
    s->ack = 0; s->pendingSequence = 0; s->pendingReplay = false; return Result::Ok;
  }
  Result r = members.delivered(key, s->incarnation, s->pendingSequence, s->pendingTimestamp);
  if (r == Result::Ok || r == Result::Duplicate) {
    s->cursor = std::max(s->cursor, s->pendingSequence); s->hintTimestamp = std::max(s->hintTimestamp, s->pendingTimestamp);
    s->ack = 0; s->pendingSequence = 0;
  }
  return r;
}
}
