#pragma once
#include "HistoryStorage.h"
#if defined(ESP32) && defined(XIAO_WIO_ROOM_HISTORY) && XIAO_WIO_ROOM_HISTORY
namespace room_history {
class SpiffsStorage : public Storage {
  bool internalFallback = false;
  bool path(const char* name, char out[96]);
public:
  bool list(const char*, FileInfo*, size_t, size_t&) override;
  bool size(const char*, size_t&) override;
  bool read(const char*, size_t, void*, size_t) override;
  bool write(const char*, const void*, size_t, bool) override;
  bool remove(const char*) override;
  bool rename(const char*, const char*) override;
  bool usage(size_t&, size_t&) override;
  void* allocate(size_t) override;
  void release(void*) override;
  bool memoryHealthy() override;
  bool usingInternalMemory() const { return internalFallback; }
};
}
#endif
