#include "crypto.h"
#include <cstring>
#include <openssl/evp.h>
#include <openssl/sha.h>

namespace mota {

void sha256_trunc(uint8_t* out, size_t out_len, const uint8_t* data, size_t len) {
  uint8_t full[32];
  SHA256(data, len, full);
  if (out_len > 32) out_len = 32;
  std::memcpy(out, full, out_len);
}

bool ed25519_verify(const uint8_t sig[64], const uint8_t* msg, size_t msg_len, const uint8_t pub[32]) {
  EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub, 32);
  if (!key) return false;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  bool ok = ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
            EVP_DigestVerify(ctx, sig, 64, msg, msg_len) == 1;
  if (ctx) EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok;
}

bool ed25519_sign(uint8_t sig[64], const uint8_t* msg, size_t msg_len, const uint8_t priv[32]) {
  EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv, 32);
  if (!key) return false;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  size_t siglen = 64;
  bool ok = ctx && EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
            EVP_DigestSign(ctx, sig, &siglen, msg, msg_len) == 1 && siglen == 64;
  if (ctx) EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(key);
  return ok;
}

bool ed25519_pub_from_priv(uint8_t pub[32], const uint8_t priv[32]) {
  EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, priv, 32);
  if (!key) return false;
  size_t len = 32;
  bool ok = EVP_PKEY_get_raw_public_key(key, pub, &len) == 1 && len == 32;
  EVP_PKEY_free(key);
  return ok;
}

bool ed25519_keygen(uint8_t priv[32], uint8_t pub[32]) {
  EVP_PKEY* key = nullptr;
  EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  bool ok = pctx && EVP_PKEY_keygen_init(pctx) == 1 && EVP_PKEY_keygen(pctx, &key) == 1;
  if (ok) {
    size_t lp = 32, lk = 32;
    ok = EVP_PKEY_get_raw_private_key(key, priv, &lp) == 1 && lp == 32 &&
         EVP_PKEY_get_raw_public_key(key, pub, &lk) == 1 && lk == 32;
  }
  if (key) EVP_PKEY_free(key);
  if (pctx) EVP_PKEY_CTX_free(pctx);
  return ok;
}

} // namespace mota
