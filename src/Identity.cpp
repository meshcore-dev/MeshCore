#include "Identity.h"
#include <string.h>
#define ED25519_NO_SEED  1
#include <ed_25519.h>
#include <Ed25519.h>

namespace mesh {

Identity::Identity() {
  memset(pub_key, 0, sizeof(pub_key));
}

Identity::Identity(const char* pub_hex) {
  Utils::fromHex(pub_key, PUB_KEY_SIZE, pub_hex);
}

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
#if 0
  // NOTE:  memory corruption bug was found in this function!!
  return ed25519_verify(sig, message, msg_len, pub_key);
#else
  return Ed25519::verify(sig, this->pub_key, message, msg_len);
#endif
}

bool Identity::readFrom(Stream& s) {
  return (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
}

bool Identity::writeTo(Stream& s) const {
  return (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
}

void Identity::printTo(Stream& s) const {
  Utils::printHex(s, pub_key, PUB_KEY_SIZE);
}

LocalIdentity::LocalIdentity() {
  memset(prv_key, 0, sizeof(prv_key));
}
LocalIdentity::LocalIdentity(const char* prv_hex, const char* pub_hex) : Identity(pub_hex) {
  Utils::fromHex(prv_key, PRV_KEY_SIZE, prv_hex);
}

LocalIdentity::LocalIdentity(RNG* rng) {
  rng->random(seed, SEED_SIZE);
  ed25519_create_keypair(pub_key, prv_key, seed);
}

bool LocalIdentity::readFrom(Stream& s) {
  bool success = (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.readBytes(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  if (success) {
    s.readBytes(seed, SEED_SIZE);
  }
  return success;
}

bool LocalIdentity::writeTo(Stream& s) const {
  bool success = (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.write(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  success = success && (s.write(seed, SEED_SIZE) == SEED_SIZE);
  return success;
}

void LocalIdentity::printTo(Stream& s) const {
  s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.println();
  s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.println();
}

size_t LocalIdentity::writePubkeyTo(uint8_t* dest, size_t max_len) {
  if (max_len < PUB_KEY_SIZE) return 0;  // not big enough
  memcpy(dest, pub_key, PUB_KEY_SIZE);
  return PUB_KEY_SIZE;
}

size_t LocalIdentity::writePrvkeyTo(uint8_t* dest, size_t max_len) {
  if (max_len < PRV_KEY_SIZE) return 0;  // not big enough
  memcpy(dest, pub_key, PRV_KEY_SIZE);
  return PRV_KEY_SIZE;
}

size_t LocalIdentity::writeSeedTo(uint8_t* dest, size_t max_len) {
  if (max_len < SEED_SIZE) return 0;  // not big enough
  memcpy(dest, pub_key, SEED_SIZE);
  return SEED_SIZE;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  if (len == PRV_KEY_SIZE + PUB_KEY_SIZE) {  // has prv + pub keys
    memcpy(prv_key, src, PRV_KEY_SIZE);
    memcpy(pub_key, &src[PRV_KEY_SIZE], PUB_KEY_SIZE);
  } else if (len == PRV_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    // now need to re-calculate the pub_key
    ed25519_derive_pub(pub_key, prv_key);
  } else if (len == SEED_SIZE) {
    memcpy(seed, src, SEED_SIZE);
    // re-generate the keypair from the given seed
    ed25519_create_keypair(pub_key, prv_key, seed);
  }
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  ed25519_sign(sig, message, msg_len, pub_key, prv_key);
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub_key) const {
  ed25519_key_exchange(secret, other_pub_key, prv_key);
}

}
