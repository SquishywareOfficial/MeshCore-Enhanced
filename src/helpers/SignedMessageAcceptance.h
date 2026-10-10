#pragma once
#include <Utils.h>
// Volatile receipt associated with a full-key ContactInfo, not a key prefix.
// Full SHA256 of author prefix + bounded text detects changed-content retries.
// Contact serialization is unchanged; this is never stored as a new disk field.
struct SignedMessageReceipt {
  uint32_t timestamp = 0;
  uint8_t digest[32] = {};
  bool valid = false;
};
template<class Enqueue> bool acceptSignedMessage(SignedMessageReceipt& receipt, uint32_t since,
  uint32_t timestamp, const uint8_t author[4], const char* text, Enqueue enqueue) {
  if (!timestamp || !text || strnlen(text, 152) > 151) return false;
  uint8_t digest[32]; mesh::Utils::sha256(digest, sizeof(digest), author, 4, (const uint8_t*)text, strlen(text));
  if (receipt.valid && receipt.timestamp <= since && timestamp == receipt.timestamp)
    return memcmp(receipt.digest, digest, sizeof(digest)) == 0;
  // Authenticated room history can be deliberately replayed with its original
  // timestamp. Do not use the normal sync watermark to reject that backfill.
  // The last accepted receipt deduplicates immediate retries; BaseChatMesh keeps
  // sync_since monotonic. Older nonconsecutive duplicates may reach the phone.
  if (!enqueue()) return false;
  receipt.timestamp = timestamp; memcpy(receipt.digest, digest, sizeof(digest)); receipt.valid = true; return true;
}
