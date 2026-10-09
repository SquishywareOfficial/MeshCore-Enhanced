#pragma once
#include "HistorySubmission.h"
#include <Utils.h>
namespace room_history {
// Actual room plain-message handler. Journal acceptance and ACK construction are
// one testable production path; the transport callback preserves direct/flood
// routing and ACK pacing in MyMesh. CLI messages never enter this function.
template<class Ack> Result receivePlain(RoomHistory& history, const uint8_t key[32], uint8_t permissions,
  uint32_t& replayTime, const uint8_t* data, size_t length, uint32_t now, uint32_t monotonic, Post& post, Ack sendAck) {
  if (length <= 5 || (data[4] >> 2) != 0) return Result::Invalid;
  uint32_t sender; memcpy(&sender, data, 4);
  size_t textLength = strnlen((const char*)data + 5, length - 5);
  if (!textLength || textLength > TextBytes || (textLength == length - 5 && length - 5 > TextBytes)) return Result::Invalid;
  char text[TextBytes + 1] = {}; memcpy(text, data + 5, textLength);
  Result result = acceptSubmission(history, key, sender, text, replayTime, (permissions & 3) >= 2, now, monotonic, post);
  if (result == Result::Ok || result == Result::Duplicate) {
    uint32_t ack; mesh::Utils::sha256((uint8_t*)&ack, 4, data, 5 + textLength, key, 32); sendAck(ack);
  }
  return result;
}
}
