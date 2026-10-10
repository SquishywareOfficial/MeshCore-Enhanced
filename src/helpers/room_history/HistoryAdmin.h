#pragma once
#include "HistoryMembers.h"
#include "HistoryClock.h"
#include "RoomHistory.h"
#include <stdio.h>
#include <ctype.h>
namespace room_history {
// Called ONLY by the existing USB/admin dispatch. Guest text never reaches it.
class HistoryAdmin {
  RoomHistory& history;
  HistoryMembers& members;
  HistoryClock& clock;
  uint64_t listingRevision = UINT64_MAX;
public:
  Result lastError = Result::Ok;
  HistoryAdmin(RoomHistory& h, HistoryMembers& m, HistoryClock& c) : history(h), members(m), clock(c) {}
  static const char* stateName(State s) { return s == State::Ready ? "ready" : s == State::Recovery ? "recovery" : "unopened"; }
  template<class Save> bool handle(const char* command, char* reply, size_t capacity, uint16_t& playback, uint64_t uptime, Save save, const char* memoryMode = "auto", uint16_t* retained = nullptr, uint32_t* floor = nullptr) {
    Result result = Result::Ok;
    // New public names retain the existing saved replay field and old CLI aliases.
    char normalized[64] = {};
    const char* key = !strncmp(command, "get ", 4) || !strncmp(command, "set ", 4) ? command + 4 : nullptr;
    if (key) {
      char name[32] = {}; const char* end = strchr(key, ' '); size_t n = end ? size_t(end - key) : strlen(key);
      if (n < sizeof(name)) for (size_t i = 0; i < n; ++i) name[i] = char(tolower((unsigned char)key[i]));
      if (!strcmp(name, "chat_replayamount") || !strcmp(name, "chat.replayamount")) {
        if (end && strlen(end) > 32) { snprintf(reply, capacity, "ERR invalid value"); return true; }
        snprintf(normalized, sizeof(normalized), "%.*s history.playback%s", 3, command, end ? end : ""); command = normalized;
      } else if (!strcmp(name, "chat_historyamount") || !strcmp(name, "chat.historyamount")) {
        if (command[0] == 'g' && !end) { snprintf(reply, capacity, "> %u", unsigned(retained ? *retained : history.retentionLimit())); return true; }
        uint64_t next = 0;
        if (command[0] != 's' || !end || !retained || !floor ||
            (strcmp(end + 1, "default") && (!parseUnsigned(end + 1, Capacity, next) || !next))) result = Result::Invalid;
        else if (history.state() != State::Ready) result = Result::Recovery;
        else {
          if (!strcmp(end + 1, "default")) next = Capacity;
          uint16_t oldAmount = *retained; uint32_t oldFloor = *floor;
          *retained = uint16_t(next); *floor = history.floorFor(*retained);
          if (!save()) { *retained = oldAmount; *floor = oldFloor; result = Result::Io; }
          else result = history.configureRetention(*retained, *floor);
        }
        if (result != Result::Ok) lastError = result;
        snprintf(reply, capacity, "%s%s", result == Result::Ok ? "" : "ERR ", error(result)); return true;
      }
    }
    if (!strcmp(command, "get history.playback")) { snprintf(reply, capacity, "> %u", unsigned(playback)); return true; }
    if (!strncmp(command, "set history.playback", 20)) {
      result = changePlayback(playback, command[20] == ' ' ? command + 21 : "", save);
    } else if (!strcmp(command, "get history")) {
      snprintf(reply, capacity, "> flash %s posts=%u/%u playback=%u oldest=%lu newest=%lu users=%u/256",
        stateName(history.state()), unsigned(history.count()), unsigned(history.retentionLimit()), unsigned(playback),
        (unsigned long)(history.count() ? history.at(0).timestamp : 0),
        (unsigned long)(history.count() ? history.at(history.count() - 1).timestamp : 0), unsigned(members.count())); return true;
    } else if (!strcmp(command, "get history.storage")) {
      size_t archive = 0, users = 0; bool valid = history.bytes(archive) && members.bytes(users);
      snprintf(reply, capacity, "> bytes=%lu+%lu index=%lu users_ram=%lu mem=%s users=%s errors=%lu tails=%lu last=%s%s",
        (unsigned long)archive, (unsigned long)users, (unsigned long)history.indexBytes(), (unsigned long)members.memoryBytes(),
        memoryMode, stateName(members.state()), (unsigned long)(history.failures + members.failures), (unsigned long)history.damagedTails,
        error(lastError), valid ? "" : " inventory-error"); return true;
    } else if (!strcmp(command, "get history.clock")) {
      uint64_t now = 0; bool known = clock.now(uptime, now);
      snprintf(reply, capacity, "> %s utc=%llu source=%s", known ? "trusted" : "untrusted", (unsigned long long)now, known ? "admin+uptime" : "none"); return true;
    } else if (!strncmp(command, "history.clock.set", 17)) {
      uint64_t epoch;
      if (command[17] != ' ' || !parseUnsigned(command + 18, UINT32_MAX, epoch) || !clock.set(epoch, uptime)) result = Result::Invalid;
    } else if (!strncmp(command, "history.users.list", 18)) {
      uint64_t page = 0;
      if (command[18] && (command[18] != ' ' || !parseUnsigned(command + 19, MemberCapacity, page))) result = Result::Invalid;
      else if (page && listingRevision != members.revision()) {
        snprintf(reply, capacity, "ERR list changed; restart page 0 rev=%llu", (unsigned long long)members.revision()); return true;
      } else {
        if (!page) listingRevision = members.revision();
        if (page >= members.count()) snprintf(reply, capacity, "> end page=%u total=%u rev=%llu", unsigned(page), unsigned(members.count()), (unsigned long long)listingRevision);
        else {
          const Member& m = members.at(uint16_t(page)); char key[65]; static const char* hex = "0123456789abcdef";
          for (size_t i = 0; i < 32; ++i) { key[i * 2] = hex[m.key[i] >> 4]; key[i * 2 + 1] = hex[m.key[i] & 15]; } key[64] = 0;
          char login[24]; if (m.loginKnown) snprintf(login, sizeof(login), "%llu", (unsigned long long)m.lastLogin); else strcpy(login, "unknown");
          snprintf(reply, capacity, "> %s delivered=%lu login=%s page=%u/%u rev=%llu", key,
            (unsigned long)m.deliveredTimestamp, login, unsigned(page), unsigned(members.count()), (unsigned long long)listingRevision);
        }
        return true;
      }
    } else if (!strncmp(command, "history.users.purge.inactive", 28)) {
      uint64_t days, now = 0; PurgeCounts counts;
      if (command[28] != ' ' || !parseUnsigned(command + 29, 36500, days) || !days) result = Result::Invalid;
      else { bool known = clock.now(uptime, now); result = members.forgetInactive(days, known, now, counts); }
      if (result == Result::Ok) { snprintf(reply, capacity, "OK removed=%u unknown=%u future=%u", counts.removed, counts.unknown, counts.future); return true; }
    } else if (!strncmp(command, "history.users.purge", 19)) {
      PurgeCounts counts; result = command[19] == ' ' ? members.forget(command + 20, counts) : Result::Invalid;
      if (result == Result::Ok) { snprintf(reply, capacity, "OK removed=%u", counts.removed); return true; }
    } else return false;
    if (result != Result::Ok) lastError = result;
    snprintf(reply, capacity, "%s%s", result == Result::Ok ? "" : "ERR ", error(result)); return true;
  }
};
}
