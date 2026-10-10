#pragma once
#include "HistoryTypes.h"
namespace room_history {
uint32_t crc32(const void*, size_t);
void put16(uint8_t*, uint16_t); void put32(uint8_t*, uint32_t); void put64(uint8_t*, uint64_t);
uint16_t get16(const uint8_t*); uint32_t get32(const uint8_t*); uint64_t get64(const uint8_t*);
bool encodePost(const Post&, uint8_t out[RecordBytes]);
bool decodePost(const uint8_t in[RecordBytes], Post&);
void encodeSegment(uint8_t out[HeaderBytes], const uint8_t key[32], uint32_t generation, uint64_t first);
bool decodeSegment(const uint8_t in[HeaderBytes], const uint8_t key[32], uint32_t generation, uint64_t& first);
constexpr size_t ControlBytes = 80; // v2; still read legacy 64-byte reservations
struct Control { uint64_t revision = 0, sequence = 0; uint32_t timestamp = 0, retentionFloor = 0; };
void encodeControl(uint8_t out[ControlBytes], const uint8_t key[32], const Control&);
bool decodeControl(const uint8_t*, const uint8_t key[32], Control&, size_t bytes = ControlBytes);
}
