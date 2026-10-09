// Radio protocol/queue tests, not cryptography tests. Keep actual Mesh,
// Dispatcher, BaseChatMesh and packet scheduling; substitute only identity crypto.
#include <Identity.h>
namespace mesh {
Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
Identity::Identity(const char* text) { Utils::fromHex(pub_key, 32, text); }
bool Identity::verify(const uint8_t*, const uint8_t*, int) const { return true; }
bool Identity::readFrom(Stream& s) { return s.readBytes(pub_key, 32) == 32; }
bool Identity::writeTo(Stream& s) const { return s.write(pub_key, 32) == 32; }
void Identity::printTo(Stream&) const {}
LocalIdentity::LocalIdentity() { memset(prv_key, 1, 64); }
LocalIdentity::LocalIdentity(const char*, const char*) { memset(prv_key, 1, 64); }
LocalIdentity::LocalIdentity(RNG*) { memset(prv_key, 1, 64); }
bool LocalIdentity::validatePrivateKey(const uint8_t*) { return true; }
bool LocalIdentity::readFrom(Stream& s) { return Identity::readFrom(s) && s.readBytes(prv_key, 64) == 64; }
bool LocalIdentity::writeTo(Stream& s) const { return Identity::writeTo(s) && s.write(prv_key, 64) == 64; }
void LocalIdentity::printTo(Stream&) const {}
size_t LocalIdentity::writeTo(uint8_t* out, size_t max) { if (max < 64) return 0; memcpy(out, prv_key, 64); if (max >= 96) memcpy(out + 64, pub_key, 32); return max >= 96 ? 96 : 64; }
void LocalIdentity::readFrom(const uint8_t* bytes, size_t n) { if (n >= 64) memcpy(prv_key, bytes, 64); if (n >= 96) memcpy(pub_key, bytes + 64, 32); }
void LocalIdentity::sign(uint8_t* sig, const uint8_t*, int) const { memset(sig, 0, 64); }
void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t*) const { memset(secret, 1, 32); }
}
