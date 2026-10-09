#pragma once
#include <errno.h>
namespace room_history {
struct SyncResult { int result, error; };
// ESP-IDF 4.4 SPIFFS has no VFS fsync hook (ENOSYS). Its checked close invokes
// SPIFFS_close, which flushes the write cache and propagates failures. Always
// check both stdio flush and backend close; never ignore real sync I/O errors.
template<class Write, class Flush, class Sync, class Close>
bool checkedSpiffsWrite(Write write, Flush flush, Sync sync, Close close) {
  bool ok = write();
  if (!flush()) ok = false;
  SyncResult completion = sync();
  if (completion.result != 0 && completion.error != ENOSYS) ok = false;
  if (!close()) ok = false;
  return ok;
}
}
