#pragma once
#include "RoomHistory.h"
#include <Stream.h>
#include <stdio.h>
namespace room_history {
// Human-readable bounded pages, USB-only. Unlike bot.read this is for a person
// using the serial console; it is not consumed by the bot JSON parser.
class UsbHistory {
  RoomHistory& history;
  static bool line(Stream& out, const char* text) {
    char b[832]; int n = snprintf(b, sizeof(b), "@chat %s\n", text);
    return n > 0 && size_t(n) < sizeof(b) && out.write((const uint8_t*)b, size_t(n)) == size_t(n);
  }
public:
  explicit UsbHistory(RoomHistory& h) : history(h) {}
  static bool isCommand(const char* s) {
    if (*s == '/') ++s;
    return !strcmp(s, "chat history") || !strncmp(s, "chat history ", 13);
  }
  bool handle(const char* command, Stream& out) {
    if (!isCommand(command)) return false;
    if (*command == '/') ++command;
    if (history.state() != State::Ready) { line(out, "ERR recovery required"); return true; }
    const char* args = command + 12; if (*args == ' ') ++args;
    const char* space = strchr(args, ' '); char number[21]; size_t n = space ? size_t(space-args) : strlen(args);
    uint64_t after = 0, limit = 4;
    if (!n || n >= sizeof(number)) { line(out, "ERR expected AFTER_ID [LIMIT 1-8]"); return true; }
    memcpy(number, args, n); number[n] = 0;
    if (!parseUnsigned(number, UINT64_MAX, after) || after > history.highWater() ||
        (space && (!parseUnsigned(space + 1, 8, limit) || !limit))) { line(out, "ERR invalid cursor or limit"); return true; }
    uint64_t next = after; unsigned sent = 0; char reply[800];
    for (uint16_t i = 0; i < history.count() && sent < limit; ++i) {
      const auto& e = history.at(i); if (e.sequence <= after) continue;
      Post p; Result r = history.read(e,p); if (r != Result::Ok) { line(out, "ERR storage failure"); return true; }
      char author[65]; const char* hex = "0123456789abcdef";
      for (size_t k = 0; k < 32; ++k) { author[2*k]=hex[p.author[k]>>4]; author[2*k+1]=hex[p.author[k]&15]; } author[64]=0;
      char text[TextBytes*4+1]; size_t j = 0;
      for (uint16_t k = 0; k < p.length; ++k) {
        char c=p.text[k];
        if (c=='\n' || c=='\r' || c=='\t' || c=='\\') { text[j++]='\\'; text[j++]=c=='\n'?'n':c=='\r'?'r':c=='\t'?'t':'\\'; }
        else if ((unsigned char)c < 32 || (unsigned char)c == 127) {
          text[j++]='\\'; text[j++]='x'; text[j++]=hex[((unsigned char)c)>>4]; text[j++]=hex[((unsigned char)c)&15];
        } else text[j++]=c;
      } text[j]=0;
      snprintf(reply,sizeof(reply),"seq=%llu timestamp=%lu author=%s text=%s",(unsigned long long)p.sequence,(unsigned long)p.timestamp,author,text);
      if (!line(out,reply)) return true;
      next=p.sequence; ++sent;
    }
    uint64_t latest=history.count()?history.at(history.count()-1).sequence:0;
    snprintf(reply,sizeof(reply),"end next=%llu count=%u more=%s gap=%s",(unsigned long long)next,sent,latest>next?"yes":"no",
      history.count() && after < history.at(0).sequence-1 ? "yes":"no");
    line(out,reply); return true;
  }
};
}
