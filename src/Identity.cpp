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
  uint8_t seed[SEED_SIZE];
  rng->random(seed, SEED_SIZE);
  ed25519_create_keypair(pub_key, prv_key, seed);
}

bool LocalIdentity::validatePrivateKey(const uint8_t prv[64]) {
    uint8_t pub[32];
    ed25519_derive_pub(pub, prv); // derive public key from given private key

    // disallow 00 or FF prefixed public keys
    if (pub[0] == 0x00 || pub[0] == 0xFF) return false;

    // known good test client keypair
    const uint8_t test_client_prv[64] = {
      0x70, 0x65, 0xe1, 0x8f, 0xd9, 0xfa, 0xbb, 0x70,
      0xc1, 0xed, 0x90, 0xdc, 0xa1, 0x99, 0x07, 0xde,
      0x69, 0x8c, 0x88, 0xb7, 0x09, 0xea, 0x14, 0x6e,
      0xaf, 0xd9, 0x3d, 0x9b, 0x83, 0x0c, 0x7b, 0x60,
      0xc4, 0x68, 0x11, 0x93, 0xc7, 0x9b, 0xbc, 0x39,
      0x94, 0x5b, 0xa8, 0x06, 0x41, 0x04, 0xbb, 0x61,
      0x8f, 0x8f, 0xd7, 0xa8, 0x4a, 0x0a, 0xf6, 0xf5,
      0x70, 0x33, 0xd6, 0xe8, 0xdd, 0xcd, 0x64, 0x71
    };
    const uint8_t test_client_pub[32] = {
      0x1e, 0xc7, 0x71, 0x75, 0xb0, 0x91, 0x8e, 0xd2,
      0x06, 0xf9, 0xae, 0x04, 0xec, 0x13, 0x6d, 0x6d,
      0x5d, 0x43, 0x15, 0xbb, 0x26, 0x30, 0x54, 0x27,
      0xf6, 0x45, 0xb4, 0x92, 0xe9, 0x35, 0x0c, 0x10
    };

    uint8_t ss1[32], ss2[32];

    // shared secret we calculte from test client pubkey and given private key
    ed25519_key_exchange(ss1, test_client_pub, prv);

    // shared secret they calculate from our derived public key and test client private key
    ed25519_key_exchange(ss2, pub, test_client_prv);

    // check that both shared secrets match
    if (memcmp(ss1, ss2, 32) != 0) return false;

    // reject all-zero shared secret
    for (int i = 0; i < 32; i++) {
        if (ss1[i] != 0) return true;
    }

    return false;
}

bool LocalIdentity::readFrom(Stream& s) {
  bool success = (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.readBytes(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  return success;
}

bool LocalIdentity::writeTo(Stream& s) const {
  bool success = (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.write(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  return success;
}

void LocalIdentity::printTo(Stream& s) const {
  s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.println();
  s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.println();
}

size_t LocalIdentity::writeTo(uint8_t* dest, size_t max_len) {
  if (max_len < PRV_KEY_SIZE) return 0;  // not big enough

  if (max_len < PRV_KEY_SIZE + PUB_KEY_SIZE) {  // only room for prv_key
    memcpy(dest, prv_key, PRV_KEY_SIZE);
    return PRV_KEY_SIZE;
  }
  memcpy(dest, prv_key, PRV_KEY_SIZE);  // otherwise can fit prv + pub keys
  memcpy(&dest[PRV_KEY_SIZE], pub_key, PUB_KEY_SIZE);
  return PRV_KEY_SIZE + PUB_KEY_SIZE;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  if (len == PRV_KEY_SIZE + PUB_KEY_SIZE) {  // has prv + pub keys
    memcpy(prv_key, src, PRV_KEY_SIZE);
    memcpy(pub_key, &src[PRV_KEY_SIZE], PUB_KEY_SIZE);
  } else if (len == PRV_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    // now need to re-calculate the pub_key
    ed25519_derive_pub(pub_key, prv_key);
  }
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  ed25519_sign(sig, message, msg_len, pub_key, prv_key);
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub_key) const {
  ed25519_key_exchange(secret, other_pub_key, prv_key);
}

}