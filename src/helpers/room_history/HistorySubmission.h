#pragma once
#include "RoomHistory.h"
namespace room_history {
// The room's actual receive path calls this before emitting its submission ACK.
// Commands/keep-alives may advance replay time, but a retained exact retry is
// still acknowledged without adding another post. Hashes are never identity.
inline Result acceptSubmission(RoomHistory& history, const uint8_t key[32], uint32_t senderTimestamp,
  const char* text, uint32_t& lastTimestamp, bool canPost, uint32_t now, uint32_t monotonic, Post& committed) {
  if (!canPost) return Result::Invalid;
  if (!text || strnlen(text, TextBytes + 1) > TextBytes) return Result::Invalid;
  Result duplicate = history.findSubmission(key, senderTimestamp, text, committed);
  if (duplicate == Result::Duplicate) return duplicate;
  if (duplicate != Result::NotFound) return duplicate;
  if (senderTimestamp < lastTimestamp) return Result::Invalid;
  Result result = history.append(key, senderTimestamp, text, 0, now, committed, monotonic);
  if (result == Result::Ok) lastTimestamp = senderTimestamp;
  return result;
}
}
