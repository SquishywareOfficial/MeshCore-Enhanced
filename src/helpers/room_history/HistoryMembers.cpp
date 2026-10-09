#include "HistoryMembers.h"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
namespace room_history {
namespace {
void encodeMember(uint8_t* b, const uint8_t* server, uint64_t revision, uint32_t epoch, uint8_t op, const Member& m) {
  memset(b, 0, MemberRecordBytes); memcpy(b, "RHM1", 4); b[4] = 1; b[5] = op; b[6] = m.loginKnown;
  memcpy(b + 8, server, 32); put64(b + 40, revision); put32(b + 48, epoch); memcpy(b + 52, m.key, 32);
  put64(b + 84, m.incarnation); put64(b + 92, m.joinFloor); put64(b + 100, m.delivered);
  put32(b + 108, m.deliveredTimestamp); put64(b + 112, m.lastLogin);
  put32(b + 120, crc32(b, 120)); memcpy(b + 124, "DONE", 4);
}
bool decodeMember(const uint8_t* b, const uint8_t* server, uint64_t& revision, uint32_t epoch, uint8_t& op, Member& m) {
  if (memcmp(b, "RHM1", 4) || b[4] != 1 || b[5] > 1 || b[6] > 1 || b[7] || memcmp(b + 8, server, 32) ||
      get32(b + 48) != epoch || get32(b + 120) != crc32(b, 120) || memcmp(b + 124, "DONE", 4)) return false;
  revision = get64(b + 40); op = b[5]; memcpy(m.key, b + 52, 32); m.loginKnown = b[6];
  m.incarnation = get64(b + 84); m.joinFloor = get64(b + 92); m.delivered = get64(b + 100);
  m.deliveredTimestamp = get32(b + 108); m.lastLogin = get64(b + 112);
  return revision && m.incarnation && m.incarnation <= revision &&
         (!m.delivered == !m.deliveredTimestamp) && (!m.delivered || m.delivered > m.joinFloor) &&
         (!m.loginKnown || (m.lastLogin >= 946684800ull && m.lastLogin <= UINT32_MAX));
}
void header(uint8_t* b, bool snapshot, const uint8_t* key, uint64_t revision, uint32_t epoch, uint16_t count, uint32_t bodyCrc) {
  memset(b, 0, HeaderBytes); memcpy(b, snapshot ? "RMS1" : "RML1", 4); put16(b + 4, 1); put16(b + 6, HeaderBytes);
  memcpy(b + 8, key, 32); put64(b + 40, revision); put32(b + 48, epoch); put16(b + 52, count);
  put32(b + 56, bodyCrc); put32(b + 60, crc32(b, 60));
}
bool decodeHeader(const uint8_t* b, bool snapshot, const uint8_t* key) {
  return !memcmp(b, snapshot ? "RMS1" : "RML1", 4) && get16(b + 4) == 1 && get16(b + 6) == HeaderBytes &&
         !memcmp(b + 8, key, 32) && get32(b + 48) && get16(b + 52) <= MemberCapacity && !b[54] && !b[55] &&
         get32(b + 60) == crc32(b, 60) && (snapshot || (!get16(b + 52) && !get32(b + 56)));
}
uint32_t combineCrc(uint32_t current, const uint8_t* b) {
  // Rolling CRC of per-record CRCs binds the complete snapshot without a big buffer.
  uint8_t pair[8]; put32(pair, current); put32(pair + 4, crc32(b, MemberRecordBytes)); return crc32(pair, sizeof(pair));
}
bool marked(const uint8_t* mask, size_t n) { return mask && (mask[n / 8] & (1u << (n % 8))); }
}
void HistoryMembers::name(char* out, bool snapshot, uint32_t gen) { snprintf(out, 40, "/rhu_%c_%08lx", snapshot ? 's' : 'l', (unsigned long)gen); }
Result HistoryMembers::fail(Result r) { ++failures; state_ = State::Recovery; return r; }
Member* HistoryMembers::find(const uint8_t* key) { for (uint16_t i = 0; i < used; ++i) if (!memcmp(table[i].key, key, 32)) return table + i; return nullptr; }
bool HistoryMembers::bytes(size_t& total) {
  FileInfo files[5]; size_t count; if (!storage.list("/rhu_", files, 5, count)) return false;
  total = 0; size_t logs = 0;
  for (size_t i = 0; i < count; ++i) {
    if (files[i].bytes > MemberBudget - total) return false; total += files[i].bytes;
    if (files[i].name[5] == 'l') logs += files[i].bytes;
  }
  return logs <= MemberLogBudget;
}
Result HistoryMembers::apply(uint8_t op, const Member& m) {
  Member* found = find(m.key);
  if (op == 1) {
    if (found) { size_t idx = found - table; for (size_t i = idx + 1; i < used; ++i) table[i - 1] = table[i]; --used; }
    return Result::Ok;
  }
  if (found) *found = m;
  else { if (used == MemberCapacity) return Result::Full; table[used++] = m; }
  return Result::Ok;
}
bool HistoryMembers::cleanup() {
  FileInfo files[5]; size_t count; if (!storage.list("/rhu_", files, 5, count)) return false;
  char snap[40], log[40]; name(snap, true, generation); name(log, false, generation);
  for (size_t i = 0; i < count; ++i)
    if (strcmp(files[i].name, snap) && strcmp(files[i].name, log) && !storage.remove(files[i].name)) return false;
  return true;
}
Result HistoryMembers::begin(const uint8_t* key) {
  memcpy(server, key, 32); used = 0; revision_ = 0; epoch_ = 1; generation = 0; logBytes = HeaderBytes;
  state_ = State::Unopened;
  if (!table) table = static_cast<Member*>(storage.allocate(sizeof(Member) * MemberCapacity));
  if (!table || !storage.memoryHealthy()) return fail(Result::Full);
  FileInfo files[5]; size_t count, total;
  if (!storage.list("/rhu_", files, 5, count) || !bytes(total)) return fail(Result::Recovery);
  std::sort(files, files + count, [](const FileInfo& a, const FileInfo& b) { return strcmp(a.name, b.name) < 0; });
  uint8_t b[MemberRecordBytes]; bool foundSnapshot = false;
  // First verify candidate snapshots completely, before selecting a generation.
  for (size_t i = 0; i < count; ++i) {
    bool snap = files[i].name[5] == 's'; char* end; unsigned long gen = strtoul(files[i].name + 7, &end, 16); char canonical[40];
    name(canonical, snap, uint32_t(gen));
    if (strlen(files[i].name) != 15 || (files[i].name[5] != 's' && files[i].name[5] != 'l') ||
        *end || !gen || gen > UINT32_MAX || strcmp(canonical, files[i].name)) return fail(Result::Recovery);
    if (!snap) {
      if (files[i].bytes >= HeaderBytes && (!storage.read(files[i].name, 0, b, HeaderBytes) || !decodeHeader(b, false, server))) return fail(Result::Recovery);
      continue;
    }
    if (files[i].bytes < HeaderBytes) continue; // interrupted staging, never authority
    if (!storage.read(files[i].name, 0, b, HeaderBytes)) return fail(Result::Io);
    if (!decodeHeader(b, true, server)) return fail(Result::Recovery);
    uint16_t n = get16(b + 52); uint64_t rev = get64(b + 40); uint32_t ep = get32(b + 48), expectedCrc = get32(b + 56), rolling = 0;
    if (files[i].bytes < HeaderBytes + n * MemberRecordBytes) continue;
    if (files[i].bytes != HeaderBytes + n * MemberRecordBytes || !rev) return fail(Result::Recovery);
    for (uint16_t row = 0; row < n; ++row) {
      Member m; uint64_t r; uint8_t op;
      if (!storage.read(files[i].name, HeaderBytes + row * MemberRecordBytes, b, MemberRecordBytes)) return fail(Result::Io);
      if (!decodeMember(b, server, r, ep, op, m) || op || r != rev) return fail(Result::Recovery);
      rolling = combineCrc(rolling, b);
    }
    if (rolling != expectedCrc) return fail(Result::Recovery);
    if (!foundSnapshot || gen > generation) { generation = uint32_t(gen); revision_ = rev; epoch_ = ep; foundSnapshot = true; }
  }
  if (count && !foundSnapshot) return fail(Result::Recovery);
  if (foundSnapshot) {
    char snap[40]; name(snap, true, generation); if (!storage.read(snap, 0, b, HeaderBytes)) return fail(Result::Io);
    uint16_t rows = get16(b + 52);
    for (uint16_t i = 0; i < rows; ++i) {
      Member m; uint64_t r; uint8_t op;
      if (!storage.read(snap, HeaderBytes + i * MemberRecordBytes, b, MemberRecordBytes) || !decodeMember(b, server, r, epoch_, op, m) || find(m.key)) return fail(Result::Recovery);
      if (apply(op, m) != Result::Ok) return fail(Result::Recovery);
    }
  }
  char log[40]; name(log, false, generation); bool tail = false, haveLog = false;
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(files[i].name, log)) continue;
    haveLog = true;
    if (files[i].bytes < HeaderBytes) { tail = true; break; }
    if (!storage.read(log, 0, b, HeaderBytes) || !decodeHeader(b, false, server) || get64(b + 40) != revision_ || get32(b + 48) != epoch_) return fail(Result::Recovery);
    size_t records = (files[i].bytes - HeaderBytes) / MemberRecordBytes;
    tail = (files[i].bytes - HeaderBytes) % MemberRecordBytes != 0;
    for (size_t row = 0; row < records; ++row) {
      Member m; uint64_t r; uint8_t op;
      if (!storage.read(log, HeaderBytes + row * MemberRecordBytes, b, MemberRecordBytes)) return fail(Result::Io);
      if (!decodeMember(b, server, r, epoch_, op, m)) {
        if (get32(b + 120) == crc32(b, 120) && !memcmp(b + 124, "DONE", 4)) return fail(Result::Recovery);
        if (row + 1 != records) return fail(Result::Recovery); tail = true; break;
      }
      if (r != revision_ + 1 || apply(op, m) != Result::Ok) return fail(Result::Recovery);
      revision_ = r;
    }
    logBytes = files[i].bytes;
  }
  state_ = State::Ready;
  if (tail || (foundSnapshot && !haveLog)) return compact(nullptr, epoch_);
  if (foundSnapshot && !cleanup()) return fail(Result::Io);
  return Result::Ok;
}
Result HistoryMembers::compact(const uint8_t* removed, uint32_t epoch) {
  if (revision_ == UINT64_MAX || generation == UINT32_MAX || !epoch) return Result::Full;
  uint64_t revision = revision_ + 1; uint32_t gen = generation + 1; uint16_t rows = 0;
  uint8_t b[MemberRecordBytes], check[MemberRecordBytes]; uint32_t rolling = 0;
  for (uint16_t i = 0; i < used; ++i) if (!marked(removed, i)) {
    ++rows; encodeMember(b, server, revision, epoch, 0, table[i]); rolling = combineCrc(rolling, b);
  }
  size_t size; if (!bytes(size) || size + HeaderBytes * 2 + rows * MemberRecordBytes > MemberBudget ||
      !headroom(storage, HeaderBytes * 2 + rows * MemberRecordBytes)) return Result::Full;
  char snap[40], log[40]; name(snap, true, gen); name(log, false, gen);
  header(b, true, server, revision, epoch, rows, rolling);
  if (!storage.write(snap, b, HeaderBytes, false)) return fail(Result::Io);
  size_t offset = HeaderBytes;
  for (uint16_t i = 0; i < used; ++i) if (!marked(removed, i)) {
    encodeMember(b, server, revision, epoch, 0, table[i]);
    if (!storage.write(snap, b, MemberRecordBytes, true) || !storage.read(snap, offset, check, MemberRecordBytes) || memcmp(b, check, MemberRecordBytes)) return fail(Result::Io);
    offset += MemberRecordBytes;
  }
  header(b, true, server, revision, epoch, rows, rolling);
  if (!storage.read(snap, 0, check, HeaderBytes) || memcmp(b, check, HeaderBytes)) return fail(Result::Io);
  header(b, false, server, revision, epoch, 0, 0);
  if (!storage.write(log, b, HeaderBytes, false) || !storage.read(log, 0, check, HeaderBytes) || memcmp(b, check, HeaderBytes)) return fail(Result::Io);
  // Publish all removals together, only after checked generation persistence.
  uint16_t out = 0; for (uint16_t i = 0; i < used; ++i) if (!marked(removed, i)) table[out++] = table[i];
  used = out; revision_ = revision; epoch_ = epoch; generation = gen; logBytes = HeaderBytes;
  if (!cleanup()) fail(Result::Io); // generation is already committed; diagnose cleanup separately
  return Result::Ok;
}
Result HistoryMembers::mutate(uint8_t operation, const Member& m) {
  if (state_ != State::Ready) return Result::Recovery;
  if (!generation || logBytes + MemberRecordBytes > 40 * 1024) {
    Result r = compact(nullptr, epoch_); if (r != Result::Ok) return r;
    if (state_ != State::Ready) return Result::Recovery;
  }
  if (revision_ == UINT64_MAX || !storage.memoryHealthy()) return Result::Full;
  size_t total; if (!bytes(total) || total + MemberRecordBytes > MemberBudget || !headroom(storage, MemberRecordBytes)) return Result::Full;
  uint8_t b[MemberRecordBytes], check[MemberRecordBytes]; char log[40]; name(log, false, generation);
  Member next = m; if (!next.incarnation) next.incarnation = revision_ + 1;
  encodeMember(b, server, revision_ + 1, epoch_, operation, next);
  if (!storage.write(log, b, sizeof(b), true) || !storage.read(log, logBytes, check, sizeof(check)) || memcmp(b, check, sizeof(b))) return fail(Result::Io);
  ++revision_; logBytes += MemberRecordBytes; return apply(operation, next);
}
Result HistoryMembers::login(const uint8_t* key, uint64_t floor, bool known, uint64_t utc, Member*& result) {
  result = nullptr; if (state_ != State::Ready) return Result::Recovery;
  if (known && (utc < 946684800ull || utc > UINT32_MAX)) return Result::Invalid;
  Member* old = find(key); Member m;
  if (old) m = *old;
  else {
    if (used == MemberCapacity || revision_ >= UINT64_MAX - 1) return Result::Full;
    m.joinFloor = floor; memcpy(m.key, key, 32);
  }
  m.loginKnown = known; if (known) m.lastLogin = utc;
  Result r = mutate(0, m); if (r == Result::Ok) result = find(key); return r;
}
Result HistoryMembers::delivered(const uint8_t* key, uint64_t incarnation, uint64_t sequence, uint32_t timestamp) {
  Member* old = find(key); if (!old || old->incarnation != incarnation) return Result::NotFound;
  if (sequence <= old->delivered) return Result::Duplicate;
  if (sequence <= old->joinFloor || !timestamp || timestamp <= old->deliveredTimestamp) return Result::Invalid;
  Member next = *old; next.delivered = sequence; next.deliveredTimestamp = timestamp; return mutate(0, next);
}
Result HistoryMembers::select(const char* prefix, uint8_t* key) {
  size_t n = prefix ? strlen(prefix) : 0; if (n < 12 || n > 64) return Result::Invalid;
  uint8_t digits[64];
  for (size_t i = 0; i < n; ++i) {
    char c = prefix[i]; if (c >= '0' && c <= '9') digits[i] = c - '0';
    else if (c >= 'a' && c <= 'f') digits[i] = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digits[i] = c - 'A' + 10; else return Result::Invalid;
  }
  Member* found = nullptr;
  for (uint16_t i = 0; i < used; ++i) {
    bool matches = true; for (size_t k = 0; k < n; ++k) if (digits[k] != ((table[i].key[k / 2] >> (k % 2 ? 0 : 4)) & 15)) { matches = false; break; }
    if (matches) { if (found) return Result::Ambiguous; found = table + i; }
  }
  if (!found) return Result::NotFound; memcpy(key, found->key, 32); return Result::Ok;
}
Result HistoryMembers::forget(const char* prefix, PurgeCounts& counts) {
  counts = PurgeCounts{}; if (state_ != State::Ready) return Result::Recovery;
  if (prefix && !strcmp(prefix, "all")) {
    if (epoch_ == UINT32_MAX) return Result::Full;
    uint8_t mask[MemberCapacity / 8]; memset(mask, 255, sizeof(mask)); uint16_t n = used;
    Result r = compact(mask, epoch_ + 1); if (r == Result::Ok) counts.removed = n; return r;
  }
  uint8_t key[32]; Result r = select(prefix, key); if (r != Result::Ok) return r;
  Member m = *find(key); r = mutate(1, m); if (r == Result::Ok) counts.removed = 1; return r;
}
Result HistoryMembers::forgetInactive(uint64_t days, bool trusted, uint64_t now, PurgeCounts& counts) {
  counts = PurgeCounts{}; if (state_ != State::Ready) return Result::Recovery;
  if (!days || days > 36500) return Result::Invalid; if (!trusted) return Result::ClockUnset;
  uint8_t mask[MemberCapacity / 8] = {}; uint64_t age = days * 86400; PurgeCounts selected;
  for (uint16_t i = 0; i < used; ++i) {
    if (!table[i].loginKnown) { ++selected.unknown; continue; }
    if (table[i].lastLogin > now) { ++selected.future; continue; }
    if (now - table[i].lastLogin >= age) { mask[i / 8] |= 1u << (i % 8); ++selected.removed; }
  }
  if (!selected.removed) { counts = selected; return Result::Ok; }
  if (epoch_ == UINT32_MAX) return Result::Full;
  Result r = compact(mask, epoch_ + 1); if (r == Result::Ok) counts = selected; return r;
}
}
