#include "SpiffsHistoryStorage.h"
#include "HistoryWrite.h"
#if defined(ESP32) && defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <esp_spiffs.h>
#include <esp_heap_caps.h>
#include <Arduino.h>
namespace room_history {
bool SpiffsStorage::path(const char* name, char out[96]) {
  if (!name || name[0] != '/' || strlen(name) >= 80 || strstr(name, "..")) return false;
  return snprintf(out, 96, "/spiffs%s", name) < 96;
}
bool SpiffsStorage::list(const char* prefix, FileInfo* out, size_t maximum, size_t& count) {
  count = 0; DIR* d = opendir("/spiffs"); if (!d) return false;
  bool ok = true;
  for (;;) {
    errno = 0; dirent* entry = readdir(d);
    if (!entry) { if (errno) ok = false; break; }
    const char* leaf = entry->d_name; while (*leaf == '/') ++leaf;
    const char* match = prefix; while (*match == '/') ++match;
    if (strncmp(leaf, match, strlen(match))) continue;
    if (count == maximum || strlen(leaf) + 2 > sizeof(out[count].name)) { ok = false; break; }
    snprintf(out[count].name, sizeof(out[count].name), "/%s", leaf);
    if (!size(out[count].name, out[count].bytes)) { ok = false; break; }
    ++count;
    delay(0);
  }
  if (closedir(d)) ok = false;
  return ok;
}
bool SpiffsStorage::size(const char* name, size_t& bytes) {
  char p[96]; struct stat s;
  if (!path(name, p) || stat(p, &s) || s.st_size < 0 || !S_ISREG(s.st_mode)) return false;
  bytes = s.st_size; return true;
}
bool SpiffsStorage::read(const char* name, size_t offset, void* data, size_t bytes) {
  char p[96]; if (!path(name, p)) return false;
  FILE* f = fopen(p, "rb"); if (!f) return false;
  bool ok = fseek(f, offset, SEEK_SET) == 0 && fread(data, 1, bytes, f) == bytes && !ferror(f);
  if (fclose(f)) ok = false;
  delay(0);
  return ok;
}
bool SpiffsStorage::write(const char* name, const void* data, size_t bytes, bool append) {
  char p[96]; if (!path(name, p)) return false;
  FILE* f = fopen(p, append ? "ab" : "wb"); if (!f) return false;
  bool ok = checkedSpiffsWrite(
    [&]() { return fwrite(data, 1, bytes, f) == bytes && !ferror(f); },
    [&]() { return fflush(f) == 0; },
    [&]() { errno = 0; int result = fsync(fileno(f)); return SyncResult{result, errno}; },
    [&]() { return fclose(f) == 0; });
  delay(0);
  return ok;
}
bool SpiffsStorage::remove(const char* name) { char p[96]; return path(name, p) && unlink(p) == 0; }
bool SpiffsStorage::rename(const char* from, const char* to) {
  char a[96], b[96]; return path(from, a) && path(to, b) && ::rename(a, b) == 0;
}
bool SpiffsStorage::usage(size_t& total, size_t& used) { return esp_spiffs_info(nullptr, &total, &used) == ESP_OK; }
void* SpiffsStorage::allocate(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) { internalFallback = true; p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
  return p;
}
void SpiffsStorage::release(void* p) { heap_caps_free(p); }
bool SpiffsStorage::memoryHealthy() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 96 * 1024 &&
         heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 32 * 1024;
}
}
#endif
