#pragma once
#include "BaseSerialInterface.h"
#include "BatteryCLI.h"

// Backported transport identifiers from upstream commit 2dbd463 (v14 CLI).
// This implements the battery subset without claiming full v14 support.
constexpr uint8_t COMPANION_CMD_RUN_CLI_COMMAND = 66;
constexpr uint8_t COMPANION_RESP_CODE_CLI_REPLY = 29;
constexpr size_t COMPANION_CLI_BODY_SIZE = 160;
constexpr size_t COMPANION_CLI_MAX_REPLY = 1 + 3 + COMPANION_CLI_BODY_SIZE - 1;
static_assert(COMPANION_CLI_MAX_REPLY <= MAX_FRAME_SIZE, "CLI reply exceeds transport frame");

// Returns zero for unrelated commands or insufficient reply capacity. Length is
// the received byte count, including opcode. Never executes a truncated command.
template<class Board, class Prefs, class Save>
size_t handleCompanionBatteryCLIFrame(Board& board, Prefs& prefs,
    const uint8_t* frame, size_t length, uint8_t* reply, size_t capacity, Save save) {
  if (!length || frame[0] != COMPANION_CMD_RUN_CLI_COMMAND ||
      capacity < COMPANION_CLI_MAX_REPLY) return 0;
  char body[COMPANION_CLI_BODY_SIZE] = {};
  size_t prefix = 0, start = 1;
  reply[0] = COMPANION_RESP_CODE_CLI_REPLY;
  auto result = [&]() {
    const size_t n = strlen(body);
    memcpy(reply + 1 + prefix, body, n);
    return 1 + prefix + n;
  };
  if (length > MAX_FRAME_SIZE) {
    snprintf(body, sizeof(body), "Error: invalid command frame");
    return result();
  }
  while (start < length && frame[start] == ' ') ++start;
  if (length - start >= 3 && frame[start] && frame[start + 1] && frame[start + 2] == '|') {
    memcpy(reply + 1, frame + start, 3);
    prefix = 3; start += 3;
  }
  size_t end = start;
  while (end < length && frame[end]) ++end;
  for (size_t i = end; i < length; ++i) {
    if (frame[i]) {
      snprintf(body, sizeof(body), "Error: invalid command frame");
      return result();
    }
  }
  while (start < end && frame[start] == ' ') ++start;
  if (start == end) {
    snprintf(body, sizeof(body), "Error: empty command");
    return result();
  }
  char command[MAX_FRAME_SIZE];
  memcpy(command, frame + start, end - start);
  command[end - start] = 0;
  if (!handleBatteryCommand(board, prefs, command, body, sizeof(body), save))
    snprintf(body, sizeof(body), "Unknown command");
  return result();
}
