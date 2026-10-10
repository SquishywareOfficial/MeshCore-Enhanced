#include "HistoryAliases.h"
namespace room_history {
bool HistoryAliases::same(const char* a, const char* b) {
  while (*a && *b) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return false;
  return *a == *b;
}
bool HistoryAliases::validName(const char* name) {
  size_t n = name ? strlen(name) : 0; if (!n || n > 31 || !strcmp(name, "-")) return false;
  bool hex = true;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    hex = hex && isxdigit(c);
  }
  return !(hex && n >= 12); // never collide with a valid key-prefix selector
}
Result HistoryAliases::validSnapshot(const char* path, uint64_t& rev, uint16_t& count) {
  size_t size; uint8_t h[HeaderBytes];
  if (!storage.size(path, size)) return Result::Io;
  if (size < HeaderBytes) return Result::NotFound; // interrupted staging
  if (!storage.read(path, 0, h, sizeof(h))) return Result::Io;
  if (get32(h + 60) != crc32(h,60)) return Result::NotFound;
  if (memcmp(h,"RHA1",4) || get16(h+4)!=1 || memcmp(h+8,room,32) || get64(h+52)) return Result::Recovery;
  count = get16(h + 6); rev = get64(h + 40);
  if (!rev || count > MemberCapacity) return Result::Recovery;
  size_t expected = HeaderBytes + count * sizeof(Alias);
  if (size < expected) return Result::NotFound;
  if (size != expected) return Result::Recovery;
  Alias* body = count ? static_cast<Alias*>(storage.allocate(count * sizeof(Alias))) : nullptr;
  if (count && (!body || !storage.memoryHealthy())) { storage.release(body); return Result::Full; }
  if (count && !storage.read(path,HeaderBytes,body,count*sizeof(Alias))) { storage.release(body); return Result::Io; }
  uint32_t digest = 0; bool valid = true;
  for (uint16_t i = 0; valid && i < count; ++i) {
    const Alias& a = body[i];
    if (!memchr(a.name, 0, sizeof(a.name)) || !validName(a.name)) { valid = false; break; }
    for (uint16_t j = 0; j < i; ++j) if (!memcmp(a.key, body[j].key, 32) || same(a.name, body[j].name)) { valid = false; break; }
    uint8_t pair[8]; put32(pair, digest); put32(pair + 4, crc32(&a, sizeof(a))); digest = crc32(pair, sizeof(pair));
  }
  storage.release(body);
  if (!valid) return Result::Recovery;
  return digest == get32(h+48) ? Result::Ok : Result::NotFound;
}
Result HistoryAliases::begin(const uint8_t* key) {
  memcpy(room, key, 32); used = 0; revision = 0; state_ = State::Unopened;
  if (!entries) entries = static_cast<Alias*>(storage.allocate(MemberCapacity * sizeof(Alias)));
  if (!entries || !storage.memoryHealthy()) { state_ = State::Recovery; return Result::Full; }
  FileInfo files[2]; size_t count;
  if (!storage.list("/rha_", files, 2, count)) { state_ = State::Recovery; return Result::Recovery; }
  const char* selected = nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(files[i].name, "/rha_a") && strcmp(files[i].name, "/rha_b")) { state_ = State::Recovery; return Result::Recovery; }
    uint64_t rev; uint16_t n;
    Result result = validSnapshot(files[i].name,rev,n);
    if (result == Result::NotFound) continue;
    if (result != Result::Ok) { state_ = State::Recovery; return result; }
    if (selected && rev == revision) {
      if (n != used) { state_ = State::Recovery; return Result::Recovery; }
      uint8_t a[256], b[256]; size_t bytes = HeaderBytes + n * sizeof(Alias);
      for (size_t offset = 0; offset < bytes; offset += sizeof(a)) {
        size_t length = bytes - offset; if (length > sizeof(a)) length = sizeof(a);
        if (!storage.read(selected, offset, a, length) || !storage.read(files[i].name, offset, b, length) || memcmp(a,b,length)) {
          state_ = State::Recovery; return Result::Recovery;
        }
      }
    }
    if (rev > revision) { selected = files[i].name; revision = rev; used = n; }
  }
  if (count && !selected) { state_ = State::Recovery; return Result::Recovery; }
  if (selected && used && !storage.read(selected, HeaderBytes, entries, used * sizeof(Alias))) { state_ = State::Recovery; return Result::Io; }
  state_ = State::Ready; return Result::Ok;
}
const char* HistoryAliases::name(const uint8_t* key) const {
  if (state_ == State::Ready) for (uint16_t i = 0; i < used; ++i) if (!memcmp(entries[i].key, key, 32)) return entries[i].name;
  return "-";
}
Result HistoryAliases::select(const char* token, HistoryMembers& members, uint8_t* key) {
  Result r = members.select(token, key);
  if (r != Result::Invalid && r != Result::NotFound) return r;
  if (!validName(token)) return r;
  if (state_ != State::Ready) return Result::Recovery;
  for (uint16_t i = 0; i < used; ++i) if (same(token, entries[i].name)) {
    if (!members.find(entries[i].key)) return Result::NotFound;
    memcpy(key, entries[i].key, 32); return Result::Ok;
  }
  return Result::NotFound;
}
Result HistoryAliases::assign(const uint8_t* key, const char* name) {
  if (state_ != State::Ready) return Result::Recovery;
  bool remove = name && !strcmp(name, "-"); if (!remove && !validName(name)) return Result::Invalid;
  uint16_t index = used;
  for (uint16_t i = 0; i < used; ++i) {
    if (!memcmp(entries[i].key, key, 32)) index = i;
    else if (!remove && same(entries[i].name, name)) return Result::Ambiguous;
  }
  if (remove && index == used) return Result::NotFound;
  if (revision == UINT64_MAX || (!remove && index == used && used == MemberCapacity)) return Result::Full;
  uint16_t nextCount = uint16_t(remove ? used - 1 : used + (index == used));
  size_t bytes = HeaderBytes + nextCount * sizeof(Alias);
  if (!headroom(storage, bytes) || !storage.memoryHealthy()) return Result::Full;
  uint8_t* buffer = static_cast<uint8_t*>(storage.allocate(bytes));
  if (!buffer || !storage.memoryHealthy()) { storage.release(buffer); return Result::Full; }
  memset(buffer, 0, bytes); uint16_t dst = 0;
  for (uint16_t i = 0; i < used; ++i) if (!(remove && i == index)) memcpy(buffer + HeaderBytes + dst++ * sizeof(Alias), entries + i, sizeof(Alias));
  if (!remove) {
    Alias updated; memcpy(updated.key, key, 32); strcpy(updated.name, name);
    memcpy(buffer + HeaderBytes + index * sizeof(Alias), &updated, sizeof(updated));
  }
  memcpy(buffer, "RHA1", 4); put16(buffer + 4, 1); put16(buffer + 6, nextCount);
  memcpy(buffer + 8, room, 32); put64(buffer + 40, revision + 1);
  uint32_t digest = 0;
  for (uint16_t i = 0; i < nextCount; ++i) {
    uint8_t pair[8]; put32(pair, digest); put32(pair + 4, crc32(buffer + HeaderBytes + i * sizeof(Alias), sizeof(Alias))); digest = crc32(pair, sizeof(pair));
  }
  put32(buffer + 48, digest); put32(buffer + 60, crc32(buffer, 60));
  const char* path = ((revision + 1) & 1) ? "/rha_a" : "/rha_b";
  bool ok = storage.write(path, buffer, bytes, false);
  uint8_t check[256];
  for (size_t off = 0; ok && off < bytes; off += sizeof(check)) {
    size_t n = bytes - off; if (n > sizeof(check)) n = sizeof(check);
    ok = storage.read(path, off, check, n) && !memcmp(check, buffer + off, n);
  }
  if (ok) { if (nextCount) memcpy(entries, buffer + HeaderBytes, nextCount * sizeof(Alias)); used = nextCount; ++revision; }
  else state_ = State::Recovery;
  storage.release(buffer); return ok ? Result::Ok : Result::Io;
}
}
