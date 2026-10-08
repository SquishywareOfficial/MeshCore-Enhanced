#pragma once
#include <stddef.h>
inline bool acceptsRepeaterCLI(bool text_message, size_t length, bool admin) {
  return text_message && length > 5 && admin;
}
