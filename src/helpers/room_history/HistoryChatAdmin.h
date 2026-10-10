#pragma once
#include "HistoryAliases.h"
#include "HistoryPlayback.h"
#include <stdio.h>
namespace room_history {
// Reachable only from the existing authenticated CLI/USB dispatch, never plain
// room-message receive. A name is a selector, not proof of access.
class HistoryChatAdmin {
  HistoryMembers& members;
  HistoryAliases& aliases;
  HistoryPlayback& playback;
  static void hex(const uint8_t* key, char* out) {
    const char* digits = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) { out[i*2] = digits[key[i] >> 4]; out[i*2+1] = digits[key[i] & 15]; } out[64] = 0;
  }
public:
  HistoryChatAdmin(HistoryMembers& m, HistoryAliases& a, HistoryPlayback& p) : members(m), aliases(a), playback(p) {}
  template<class Active, class Cancel> bool handle(const char* command, char* reply, size_t capacity, Active active, Cancel cancel) {
    if (command[0] == '/') ++command;
    if (strcmp(command, "chat") && strncmp(command, "chat ", 5)) return false;
    const char* args = command + (strlen(command) >= 5 ? 5 : 4);
    char text[128] = {}; const char* tokens[4] = {}; size_t length = strlen(args), count = 0;
    if (length >= sizeof(text)) { snprintf(reply, capacity, "ERR command too long"); return true; }
    memcpy(text, args, length + 1); char* p = text;
    while (*p) {
      while (*p == ' ') ++p; if (!*p) break;
      if (count == 4) { snprintf(reply, capacity, "ERR unexpected argument"); return true; }
      tokens[count++] = p; while (*p && *p != ' ') ++p; if (*p) *p++ = 0;
    }
    Result r = Result::Invalid; uint8_t key[32];
    if (members.state() != State::Ready) r = Result::Recovery;
    else if (count && !strcmp(tokens[0], "aliases") && count <= 2) {
      uint64_t page = 0;
      if (aliases.state() != State::Ready) r = Result::Recovery;
      else if (count == 2 && !parseUnsigned(tokens[1],MemberCapacity,page)) r = Result::Invalid;
      else if (page >= aliases.count()) { snprintf(reply,capacity,"> end page=%u total=%u",unsigned(page),unsigned(aliases.count())); return true; }
      else {
        const Alias& a=aliases.at(uint16_t(page));char full[65];hex(a.key,full);
        snprintf(reply,capacity,"> %s alias=%s member=%s page=%u/%u",full,a.name,members.find(a.key)?"yes":"no",unsigned(page),unsigned(aliases.count()));return true;
      }
    }
    else if (count && !strcmp(tokens[0], "users") && count <= 2) {
      uint64_t page = 0;
      if (count == 2 && !parseUnsigned(tokens[1], MemberCapacity, page)) r = Result::Invalid;
      else if (page >= members.count()) { snprintf(reply, capacity, "> end page=%u total=%u", unsigned(page), unsigned(members.count())); return true; }
      else {
        const Member& m = members.at(uint16_t(page)); char full[65]; hex(m.key, full);
        snprintf(reply, capacity, "> %s alias=%s seq=%llu page=%u/%u", full, aliases.name(m.key), (unsigned long long)m.delivered, unsigned(page), unsigned(members.count())); return true;
      }
    } else if (count == 3 && !strcmp(tokens[0], "user.alias")) {
      r = members.select(tokens[1], key);
      // A purged member's alias is deliberately not an authorization source.
      // Still allow removing its saved alias with the exact full public key.
      if (r == Result::NotFound && !strcmp(tokens[2], "-") && strlen(tokens[1]) == 64) {
        r = Result::Ok;
        for (size_t i = 0; i < 64; ++i) {
          unsigned char c = (unsigned char)tolower((unsigned char)tokens[1][i]);
          if (!isxdigit(c)) { r = Result::Invalid; break; }
          uint8_t digit = c <= '9' ? c - '0' : c - 'a' + 10;
          if (!(i & 1)) key[i/2] = digit << 4; else key[i/2] |= digit;
        }
      }
      if (r == Result::Ok) r = aliases.assign(key, tokens[2]);
    } else if (count == 4 && !strcmp(tokens[0], "replay")) {
      uint64_t older, newer;
      if (!parseUnsigned(tokens[2], Capacity - 1, older) || !parseUnsigned(tokens[3], Capacity - 1, newer) || older < newer) r = Result::Invalid;
      else if ((r = aliases.select(tokens[1], members, key)) == Result::Ok) {
        if (!active(key)) { snprintf(reply, capacity, "ERR user must be logged in"); return true; }
        r = playback.replay(key, uint16_t(older), uint16_t(newer));
        if (r == Result::Ok) {
          auto s = playback.session(key);
          snprintf(reply, capacity, "OK queued=%u first=%llu last=%llu", unsigned(older-newer+1), (unsigned long long)s->replayFirst, (unsigned long long)s->replayLast); return true;
        }
      }
    } else if (count == 2 && (!strcmp(tokens[0], "replay.status") || !strcmp(tokens[0], "replay.cancel"))) {
      r = aliases.select(tokens[1], members, key);
      if (r == Result::Ok) {
        auto s = playback.session(key);
        if (!s) r = Result::NotFound;
        else if (!strcmp(tokens[0], "replay.cancel")) {
          bool pending = s->pendingReplay;
          r = playback.cancelReplay(key); if (r == Result::Ok && pending) cancel(key);
        } else {
          const char* state = s->replayActive ? "active" : !s->replayFirst ? "none" : s->replayCursor >= s->replayLast ? "complete" : "cancelled/expired";
          snprintf(reply, capacity, "> %s first=%llu last=%llu cursor=%llu remaining=%u expired=%u", state,
            (unsigned long long)s->replayFirst, (unsigned long long)s->replayLast, (unsigned long long)s->replayCursor,
            unsigned(playback.replayCount(*s)), unsigned(s->replayExpired)); return true;
        }
      }
    }
    snprintf(reply, capacity, "%s%s", r == Result::Ok ? "" : "ERR ", error(r)); return true;
  }
};
}
