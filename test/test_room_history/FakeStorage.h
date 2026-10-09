#pragma once
#include <helpers/room_history/HistoryStorage.h>
#include <map>
#include <string>
#include <vector>
#include <stdlib.h>
#include <algorithm>
class FakeStorage : public room_history::Storage {
public:
  std::map<std::string, std::vector<uint8_t>> files;
  size_t total = 1438481, foreignUsed = 2259;
  int operations = 0, failAt = -1; size_t shortWrite = SIZE_MAX;
  bool failReads = false, failAlloc = false, failUnlink = false, lowMemory = false;
  bool succeeds() { return ++operations != failAt; }
  bool list(const char* prefix, room_history::FileInfo* out, size_t max, size_t& n) override {
    if (failReads) return false; n = 0;
    for (auto& f : files) if (f.first.find(prefix) == 0) {
      if (n == max || f.first.size() >= sizeof(out[n].name)) return false;
      strcpy(out[n].name, f.first.c_str()); out[n++].bytes = f.second.size();
    }
    return true;
  }
  bool size(const char* name, size_t& n) override {
    if (failReads || !files.count(name)) return false; n = files[name].size(); return true;
  }
  bool read(const char* name, size_t offset, void* p, size_t n) override {
    if (failReads || !files.count(name) || offset > files[name].size() || n > files[name].size() - offset) return false;
    memcpy(p, files[name].data() + offset, n); return true;
  }
  bool write(const char* name, const void* p, size_t n, bool append) override {
    bool ok = succeeds(); auto& f = files[name]; if (!append) f.clear();
    size_t written = ok ? n : std::min(n, shortWrite);
    auto b = static_cast<const uint8_t*>(p); f.insert(f.end(), b, b + written);
    // A failed sync/close may leave a complete record. Caller still gets false.
    return ok;
  }
  bool remove(const char* name) override {
    if (failUnlink || !succeeds()) return false; return files.erase(name) == 1;
  }
  bool rename(const char* from, const char* to) override {
    if (!succeeds() || !files.count(from) || files.count(to)) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
  bool usage(size_t& capacity, size_t& used) override {
    capacity = total; used = foreignUsed; for (auto& f : files) used += f.second.size(); return true;
  }
  void* allocate(size_t n) override { return failAlloc ? nullptr : malloc(n); }
  void release(void* p) override { free(p); }
  bool memoryHealthy() override { return !lowMemory; }
};
