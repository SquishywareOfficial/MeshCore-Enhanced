#pragma once
#include "HistoryTypes.h"

namespace room_history {
// Names are flat, bounded, application-owned paths. All writes include checked
// backend cache completion AND close (fsync where supported). On failure an
// adapter may have written a prefix: recovery must
// validate bytes, rather than assume a failed operation changed nothing.
struct FileInfo { char name[40]; size_t bytes; };
class Storage {
public:
  virtual ~Storage() {}
  virtual bool list(const char* prefix, FileInfo* out, size_t maximum, size_t& count) = 0;
  virtual bool size(const char* name, size_t& bytes) = 0;
  virtual bool read(const char* name, size_t offset, void* data, size_t bytes) = 0;
  virtual bool write(const char* name, const void* data, size_t bytes, bool append) = 0;
  virtual bool remove(const char* name) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool usage(size_t& total, size_t& used) = 0;
  virtual void* allocate(size_t bytes) = 0;
  virtual void release(void* p) = 0;
  virtual bool memoryHealthy() = 0;
};
inline bool headroom(Storage& storage, size_t extra) {
  size_t total, used;
  return storage.usage(total, used) && used <= total && extra <= total - used &&
         used + extra <= total / 100 * 65 && total - used - extra >= 256 * 1024;
}
}
