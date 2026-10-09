#pragma once
#include "HistoryStorage.h"
namespace room_history {
template<class Reader> bool partitionIsErased(size_t bytes, Reader read) {
  if (!bytes || bytes > 8 * 1024 * 1024) return false;
  uint8_t block[512];
  for (size_t offset = 0; offset < bytes; offset += sizeof(block)) {
    size_t n = bytes - offset; if (n > sizeof(block)) n = sizeof(block);
    if (!read(offset, block, n)) return false;
    for (size_t i = 0; i < n; ++i) if (block[i] != 0xff) return false;
  }
  return true;
}
template<class Backend> bool safeMount(Backend& b) {
  if (b.mount()) return true;
  return b.erased() && b.format() && b.mount();
}
enum class IdentityAction { Load, Provision, Recovery };
inline IdentityAction identityAction(size_t files, bool exists, bool valid) {
  return exists ? (valid ? IdentityAction::Load : IdentityAction::Recovery)
                : (files == 0 ? IdentityAction::Provision : IdentityAction::Recovery);
}
template<class Validator> bool recoverPreferences(Storage& storage, Validator valid) {
  FileInfo files[4]; size_t count;
  if (!storage.list("/prefs.", files, 4, count)) return false;
  bool current = false, previous = false, failed = false;
  for (size_t i = 0; i < count; ++i) {
    current |= strcmp(files[i].name, "/prefs.json") == 0;
    previous |= strcmp(files[i].name, "/prefs.previous") == 0;
    failed |= strcmp(files[i].name, "/prefs.failed") == 0;
  }
  if (current && valid("/prefs.json")) return true;
  if (previous && valid("/prefs.previous")) {
    if (current && (failed || !storage.rename("/prefs.json", "/prefs.failed"))) return false;
    return storage.rename("/prefs.previous", "/prefs.json");
  }
  if (current || previous) return false;
  size_t bytes;
  return count == 0 || storage.size("/com_prefs", bytes);
}
}
