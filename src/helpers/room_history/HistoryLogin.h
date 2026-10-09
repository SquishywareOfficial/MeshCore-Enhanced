#pragma once
#include "HistoryTypes.h"
namespace room_history {
inline bool authenticateLogin(const char* supplied, bool knownAclKey, uint8_t knownPermissions,
  const char* adminPassword, const char* guestPassword, bool allowReadOnly, uint8_t& permissions) {
  if (!supplied) return false;
  if (!*supplied && knownAclKey) { permissions = knownPermissions; return true; }
  if (!strcmp(supplied, adminPassword)) permissions = 3;
  else if (!strcmp(supplied, guestPassword)) permissions = 2;
  else if (allowReadOnly) permissions = 0;
  else return false;
  return true;
}
}
