#include "HistoryCodec.h"
namespace room_history {
uint32_t crc32(const void* buffer, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(buffer); uint32_t crc = UINT32_MAX;
  while (n--) { crc ^= *p++; for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
  return ~crc;
}
void put16(uint8_t* p, uint16_t v) { for (int i = 0; i < 2; ++i) { p[i] = uint8_t(v); v >>= 8; } }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) { p[i] = uint8_t(v); v >>= 8; } }
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) { p[i] = uint8_t(v); v >>= 8; } }
uint16_t get16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; --i) v = (v << 8) | p[i]; return v; }
uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }
bool encodePost(const Post& p, uint8_t* out) {
  if (!p.sequence || !p.timestamp || p.kind > 1 || p.length > TextBytes || strnlen(p.text, TextBytes + 1) != p.length ||
      (p.kind == 1 && p.senderTimestamp)) return false;
  memset(out, 0, RecordBytes); memcpy(out, "RHR1", 4); out[4] = p.kind;
  put16(out + 6, HeaderBytes); put16(out + 8, RecordBytes); put16(out + 10, p.length);
  put64(out + 12, p.sequence); put32(out + 20, p.timestamp); put32(out + 24, p.senderTimestamp);
  memcpy(out + 28, p.author, 32); memcpy(out + 64, p.text, p.length);
  put32(out + 216, crc32(out, 216)); memcpy(out + 220, "DONE", 4); return true;
}
bool decodePost(const uint8_t* in, Post& p) {
  if (memcmp(in, "RHR1", 4) || in[4] > 1 || in[5] || get16(in + 6) != HeaderBytes ||
      get16(in + 8) != RecordBytes || get16(in + 10) > TextBytes ||
      memcmp(in + 220, "DONE", 4) || get32(in + 216) != crc32(in, 216) || get32(in + 60) || in[215]) return false;
  uint16_t len = get16(in + 10);
  for (size_t i = 0; i < TextBytes; ++i) if ((i < len && !in[64 + i]) || (i >= len && in[64 + i])) return false;
  p = Post{}; p.sequence = get64(in + 12); p.timestamp = get32(in + 20);
  p.senderTimestamp = get32(in + 24); p.kind = in[4]; p.length = len;
  memcpy(p.author, in + 28, 32); memcpy(p.text, in + 64, len);
  return p.sequence && p.timestamp && (p.kind != 1 || !p.senderTimestamp);
}
void encodeSegment(uint8_t* out, const uint8_t* key, uint32_t generation, uint64_t first) {
  memset(out, 0, HeaderBytes); memcpy(out, "RHS1", 4); put16(out + 4, 1); put16(out + 6, HeaderBytes);
  put32(out + 8, generation); put16(out + 12, SegmentSlots); put16(out + 14, RecordBytes);
  memcpy(out + 16, key, 32); put64(out + 48, first); put32(out + 60, crc32(out, 60));
}
bool decodeSegment(const uint8_t* in, const uint8_t* key, uint32_t generation, uint64_t& first) {
  if (memcmp(in, "RHS1", 4) || get16(in + 4) != 1 || get16(in + 6) != HeaderBytes ||
      get32(in + 8) != generation || get16(in + 12) != SegmentSlots || get16(in + 14) != RecordBytes ||
      memcmp(in + 16, key, 32) || get32(in + 56) || get32(in + 60) != crc32(in, 60)) return false;
  first = get64(in + 48); return first && generation;
}
void encodeControl(uint8_t* out, const uint8_t* key, const Control& c) {
  memset(out, 0, HeaderBytes); memcpy(out, "RHC1", 4); put16(out + 4, 1); put16(out + 6, HeaderBytes);
  put64(out + 8, c.revision); memcpy(out + 16, key, 32);
  put64(out + 48, c.sequence); put32(out + 56, c.timestamp); put32(out + 60, crc32(out, 60));
}
bool decodeControl(const uint8_t* in, const uint8_t* key, Control& c) {
  if (memcmp(in, "RHC1", 4) || get16(in + 4) != 1 || get16(in + 6) != HeaderBytes ||
      memcmp(in + 16, key, 32) || get32(in + 60) != crc32(in, 60)) return false;
  c.revision = get64(in + 8); c.sequence = get64(in + 48); c.timestamp = get32(in + 56);
  return c.revision && ((!c.sequence && !c.timestamp) || (c.sequence && c.timestamp));
}
}
