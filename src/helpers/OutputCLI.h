#pragma once
#include "OutputControl.h"
#include "BatteryCLI.h"
#include <stdio.h>
#include <string.h>

// Called by the common USB/admin CLI. save is invoked only for assignments.
template<class Board, class Save>
bool handleOutputCommand(Board& board, uint8_t& stored_mask,
                         const char* command, char* reply, Save save) {
  if (strcmp(command, "output") && strncmp(command, "output ", 7)) return false;
  auto error = [reply](const char* message) { snprintf(reply, 160, "%s", message); };
  if (!strcmp(command, "output status")) {
    const int mask = board.getOutputMask();
    if (mask < 0) { error("Error: unsupported"); return true; }
    size_t used = 0;
    snprintf(reply, 160, "> none");
    for (int i = 0; i < 5; ++i) {
      if (!(mask & (1u << i))) continue;
      const int gpio = mesh::outputGpioAt(i);
      used += snprintf(reply + used, 160 - used, "%s%d=%s", used ? " " : "> ",
                       gpio, board.getOutputState(gpio) == 1 ? "on" : "off");
    }
    return true;
  }
  mesh::OutputOperation op;
  const char* token = nullptr;
  if (!strncmp(command, "output configure ", 17)) {
    op = mesh::OutputOperation::Configure; token = command + 17;
  } else if (!strncmp(command, "output on ", 10)) {
    op = mesh::OutputOperation::On; token = command + 10;
  } else if (!strncmp(command, "output off ", 11)) {
    op = mesh::OutputOperation::Off; token = command + 11;
  } else if (!strncmp(command, "output remove ", 14)) {
    op = mesh::OutputOperation::Remove; token = command + 14;
  } else { error("Error: usage"); return true; }
  if (!*token) { error("Error: usage"); return true; }
  int gpio = 0;
  bool too_large = false;
  for (const char* p = token; *p; ++p) {
    if (*p < '0' || *p > '9') { error("Error: usage"); return true; }
    // Continue checking syntax after overflow without overflowing an integer.
    if (gpio > 100 || too_large) too_large = true;
    else gpio = gpio * 10 + (*p - '0');
  }
  if (too_large || !mesh::outputBitForGpio(gpio)) {
    error("Error: invalid GPIO"); return true;
  }
  bool changed = false;
  const auto result = board.controlOutput(op, gpio, changed);
  switch (result) {
    case mesh::OutputResult::Success:
      if (changed) { stored_mask = uint8_t(board.getOutputMask()); save(); }
      error("OK"); break;
    case mesh::OutputResult::Unsupported: error("Error: unsupported"); break;
    case mesh::OutputResult::InvalidGpio: error("Error: invalid GPIO"); break;
    case mesh::OutputResult::Reserved: error("Error: GPIO reserved"); break;
    case mesh::OutputResult::NotConfigured: error("Error: output not configured"); break;
  }
  return true;
}

// Startup ordering is shared with native tests. A corrupt battery selection
// reserves the default pin and leaves sensing off. No preferences are saved here.
template<class Board, class Prefs>
void restoreOptionalIO(Board& board, Prefs& prefs) {
  restoreBatterySettings(board, prefs);
  if (board.getOutputMask() >= 0) prefs.outputs_mask = board.restoreOutputs(prefs.outputs_mask);
}
