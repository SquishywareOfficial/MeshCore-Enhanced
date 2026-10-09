#pragma once
#include "HistoryTypes.h"
namespace room_history {
class HistoryClock {
  uint64_t anchor = 0, uptime = 0;
  bool trusted = false;
public:
  // Explicit civil time only. Message uniqueness floors never establish trust.
  bool set(uint64_t epoch, uint64_t monotonicMillis) {
    if (epoch < 946684800ull || epoch > UINT32_MAX) return false;
    anchor = epoch; uptime = monotonicMillis; trusted = true; return true;
  }
  bool now(uint64_t monotonicMillis, uint64_t& utc) const {
    if (!trusted || monotonicMillis < uptime) return false;
    uint64_t seconds = (monotonicMillis - uptime) / 1000;
    if (seconds > UINT32_MAX - anchor) return false;
    utc = anchor + seconds; return true;
  }
};
}
