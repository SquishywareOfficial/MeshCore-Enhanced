#include "RoomHistory.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
namespace room_history {
void RoomHistory::segmentName(uint32_t n, char out[40]) { snprintf(out, 40, "/rh_s_%08lx", (unsigned long)n); }
Result RoomHistory::failed(Result r) { ++failures; status = State::Recovery; return r; }
void RoomHistory::insert(const Post& p, uint32_t gen, uint16_t slot) {
  uint16_t idx = (start + used) % Capacity;
  if (used == Capacity) { idx = start; start = (start + 1) % Capacity; } else ++used;
  entries[idx] = {p.sequence, p.timestamp, p.senderTimestamp, gen, crc32(p.author, 32),
                  crc32(p.text, p.length), slot, p.kind, 0};
}
bool RoomHistory::bytes(size_t& n) {
  FileInfo files[MaxSegments + 2]; size_t count;
  if (!storage.list("/rh_", files, MaxSegments + 2, count)) return false;
  n = 0; for (size_t i = 0; i < count; ++i) {
    if (files[i].bytes > ArchiveBudget - n) return false; n += files[i].bytes;
  }
  return true;
}
Result RoomHistory::reserve(uint64_t sequence, uint32_t timestamp) {
  if (control.revision == UINT64_MAX || !sequence || !timestamp) return Result::Full;
  Control next; next.revision = control.revision + 1; next.sequence = sequence; next.timestamp = timestamp;
  uint8_t data[HeaderBytes], verify[HeaderBytes];
  encodeControl(data, server, next); const char* name = (next.revision & 1) ? "/rh_ctl_a" : "/rh_ctl_b";
  if (!headroom(storage, HeaderBytes) || !storage.write(name, data, sizeof(data), false) ||
      !storage.read(name, 0, verify, sizeof(verify)) || memcmp(data, verify, sizeof(data))) return failed(Result::Io);
  control = next; return Result::Ok;
}
Result RoomHistory::begin(const uint8_t* key) {
  status = State::Unopened; start = used = 0; generation = 0; slots = SegmentSlots; control = Control{};
  memcpy(server, key, 32);
  if (!entries) entries = static_cast<IndexEntry*>(storage.allocate(sizeof(IndexEntry) * Capacity));
  if (!entries || !storage.memoryHealthy()) return failed(Result::Full);
  FileInfo files[MaxSegments + 2]; size_t count;
  if (!storage.list("/rh_", files, MaxSegments + 2, count)) return failed(Result::Recovery);
  size_t total = 0, segmentCount = 0; bool haveControl = false;
  for (size_t i = 0; i < count; ++i) {
    if (files[i].bytes > ArchiveBudget - total) return failed(Result::Recovery); total += files[i].bytes;
    if (strcmp(files[i].name, "/rh_ctl_a") == 0 || strcmp(files[i].name, "/rh_ctl_b") == 0) {
      uint8_t b[HeaderBytes]; Control candidate;
      if (files[i].bytes == HeaderBytes && !storage.read(files[i].name, 0, b, sizeof(b))) return failed(Result::Io);
      if (files[i].bytes == HeaderBytes && get32(b + 60) == crc32(b, 60) &&
          (memcmp(b, "RHC1", 4) || get16(b + 4) != 1 || memcmp(b + 16, server, 32))) return failed(Result::Recovery);
      if (files[i].bytes == HeaderBytes && decodeControl(b, server, candidate)) {
        if (haveControl && candidate.revision == control.revision && (candidate.sequence != control.sequence || candidate.timestamp != control.timestamp)) return failed(Result::Recovery);
        if (!haveControl || candidate.revision > control.revision) control = candidate;
        haveControl = true;
      }
    } else if (strncmp(files[i].name, "/rh_s_", 6)) return failed(Result::Recovery);
  }
  if (count && !haveControl) return failed(Result::Recovery);
  // Sort tiny file inventory, not the 2,000-record index, before scanning.
  std::sort(files, files + count, [](const FileInfo& a, const FileInfo& b) { return strcmp(a.name, b.name) < 0; });
  uint64_t lastSequence = 0; uint32_t lastTimestamp = 0;
  for (size_t i = 0; i < count; ++i) {
    if (strncmp(files[i].name, "/rh_s_", 6)) continue;
    if (++segmentCount > MaxSegments || strlen(files[i].name) != 14) return failed(Result::Recovery);
    char* end; unsigned long gen = strtoul(files[i].name + 6, &end, 16); char canonical[40]; segmentName(uint32_t(gen), canonical);
    if (*end || !gen || gen > UINT32_MAX || strcmp(canonical, files[i].name) || files[i].bytes > HeaderBytes + SegmentSlots * RecordBytes) return failed(Result::Recovery);
    generation = uint32_t(gen); slots = SegmentSlots;
    // A cut while creating the LAST segment header is recoverable only with
    // an independently valid reservation; preserve its name and start a new one.
    if (files[i].bytes < HeaderBytes) {
      if (i + 1 != count || !haveControl || control.sequence <= lastSequence || control.timestamp <= lastTimestamp) return failed(Result::Recovery);
      ++damagedTails; continue;
    }
    uint8_t b[RecordBytes]; uint64_t first;
    if (!storage.read(files[i].name, 0, b, HeaderBytes) || !decodeSegment(b, server, generation, first)) return failed(Result::Recovery);
    size_t recordCount = (files[i].bytes - HeaderBytes) / RecordBytes;
    bool tail = (files[i].bytes - HeaderBytes) % RecordBytes != 0;
    for (uint16_t k = 0; k < recordCount; ++k) {
      Post p;
      if (!storage.read(files[i].name, HeaderBytes + k * RecordBytes, b, RecordBytes)) return failed(Result::Io);
      if (!decodePost(b, p)) {
        if (get32(b + 216) == crc32(b, 216) && !memcmp(b + 220, "DONE", 4)) return failed(Result::Recovery);
        if (k + 1 != recordCount) return failed(Result::Recovery);
        tail = true; break;
      }
      if (p.sequence < first || p.sequence <= lastSequence || p.timestamp <= lastTimestamp || (p.kind && memcmp(p.author, server, 32))) return failed(Result::Recovery);
      insert(p, generation, k); lastSequence = p.sequence; lastTimestamp = p.timestamp;
    }
    if (tail) {
      if (control.sequence <= lastSequence || control.timestamp <= lastTimestamp) return failed(Result::Recovery);
      ++damagedTails; slots = SegmentSlots;
    }
    else slots = uint16_t(recordCount);
  }
  if (lastSequence > control.sequence || lastTimestamp > control.timestamp) {
    if (reserve(std::max(lastSequence, control.sequence), std::max(lastTimestamp, control.timestamp)) != Result::Ok) return Result::Io;
  }
  status = State::Ready;
  return prune();
}
Result RoomHistory::prune() {
  FileInfo files[MaxSegments]; size_t count;
  if (!storage.list("/rh_s_", files, MaxSegments, count)) return failed(Result::Recovery);
  uint32_t oldest = used ? at(0).generation : generation;
  for (size_t i = 0; i < count; ++i) {
    unsigned long gen = strtoul(files[i].name + 6, nullptr, 16);
    if (gen < oldest && !storage.remove(files[i].name)) return failed(Result::Io);
  }
  return Result::Ok;
}
Result RoomHistory::read(const IndexEntry& entry, Post& out) {
  uint8_t b[RecordBytes]; char name[40]; segmentName(entry.generation, name);
  if (!storage.read(name, HeaderBytes + entry.slot * RecordBytes, b, sizeof(b))) return failed(Result::Io);
  if (!decodePost(b, out) || out.sequence != entry.sequence || out.timestamp != entry.timestamp) return failed(Result::Recovery);
  return Result::Ok;
}
Result RoomHistory::findSubmission(const uint8_t* author, uint32_t senderTime, const char* text, Post& found) {
  if (status != State::Ready) return Result::Recovery;
  uint32_t hash = crc32(author, 32); bool conflict = false;
  for (uint16_t k = 0; k < used; ++k) {
    const auto& entry = at(k);
    if (entry.kind || entry.authorHash != hash || entry.senderTimestamp != senderTime) continue;
    Post p; if (read(entry, p) != Result::Ok) return Result::Recovery;
    if (memcmp(p.author, author, 32)) continue;
    if (strcmp(text, p.text) == 0) { found = p; return Result::Duplicate; }
    conflict = true;
  }
  return conflict ? Result::Conflict : Result::NotFound;
}
Result RoomHistory::append(const uint8_t* author, uint32_t senderTime, const char* text, uint8_t kind, uint32_t now, Post& committed, uint32_t monotonicMillis) {
  if (status != State::Ready) return Result::Recovery;
  if (!text || kind > 1 || (kind && (senderTime || memcmp(author, server, 32)))) return Result::Invalid;
  size_t length = strnlen(text, TextBytes + 1); if (!length || length > TextBytes) return Result::Invalid;
  if (!kind) { Result found = findSubmission(author, senderTime, text, committed); if (found != Result::NotFound) return found; }
  if (control.sequence == UINT64_MAX || control.timestamp == UINT32_MAX || !storage.memoryHealthy()) return Result::Full;
  size_t size; if (!bytes(size)) return failed(Result::Recovery);
  const size_t growth = RecordBytes + HeaderBytes + (slots == SegmentSlots ? HeaderBytes : 0);
  if (size + growth > ArchiveBudget || !headroom(storage, growth)) return Result::Full;
  if (slots == SegmentSlots) {
    FileInfo files[MaxSegments]; size_t fileCount;
    if (!storage.list("/rh_s_", files, MaxSegments, fileCount) || fileCount >= MaxSegments) return Result::Full;
  }
  uint64_t seq = control.sequence + 1; uint32_t time = std::max(now, control.timestamp + 1);
  Result result = reserve(seq, time); if (result != Result::Ok) return result;
  char name[40]; uint8_t b[RecordBytes], verify[RecordBytes];
  if (slots == SegmentSlots) {
    if (generation == UINT32_MAX) return failed(Result::Full);
    ++generation; segmentName(generation, name);
    encodeSegment(b, server, generation, seq);
    if (!storage.write(name, b, HeaderBytes, false) || !storage.read(name, 0, verify, HeaderBytes) || memcmp(b, verify, HeaderBytes)) return failed(Result::Io);
    slots = 0;
  } else segmentName(generation, name);
  Post p; p.sequence = seq; p.timestamp = time; p.senderTimestamp = senderTime; p.kind = kind;
  memcpy(p.author, author, 32); p.length = uint16_t(length); memcpy(p.text, text, length);
  if (!encodePost(p, b) || !storage.write(name, b, RecordBytes, true) ||
      !storage.read(name, HeaderBytes + slots * RecordBytes, verify, RecordBytes) || memcmp(b, verify, RecordBytes)) return failed(Result::Io);
  insert(p, generation, slots++); committed = p;
  // This volatile delay is independent of potentially future logical wire time.
  auto& newest = entries[(start + used - 1) % Capacity]; newest.reserved = 1; newest.readyAt = monotonicMillis + 6000;
  // A cleanup failure after a checked append cannot make the author retry a
  // different post: the committed record remains discoverable after recovery.
  prune(); return Result::Ok;
}
}
