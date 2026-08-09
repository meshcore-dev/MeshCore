#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Real SHA-256 for native/host tests. Mirrors the rweather/Crypto streaming API used by
// src/Utils.cpp (update / finalize-with-truncation, plus resetHMAC / finalizeHMAC), so that
// Utils::sha256(...) produces correct digests on the host and OTA merkle tests are meaningful.
// (On-device the real rweather <SHA256.h> is used instead of this mock.)
class SHA256 {
  uint32_t h[8];
  uint8_t buf[64];
  uint32_t buf_len;
  uint64_t total_len;
  uint8_t hmac_key[64];

  static uint32_t ror(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

  void process(const uint8_t* p) {
    static const uint32_t K[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) | ((uint32_t)p[i*4+2] << 8) | p[i*4+3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15] >> 3);
      uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + maj;
      hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  }

public:
  SHA256() { reset(); }

  void reset() {
    h[0]=0x6a09e667; h[1]=0xbb67ae85; h[2]=0x3c6ef372; h[3]=0xa54ff53a;
    h[4]=0x510e527f; h[5]=0x9b05688c; h[6]=0x1f83d9ab; h[7]=0x5be0cd19;
    buf_len = 0; total_len = 0;
  }

  void update(const uint8_t* data, size_t n) {
    total_len += n;
    while (n) {
      size_t take = 64 - buf_len; if (take > n) take = n;
      memcpy(buf + buf_len, data, take); buf_len += (uint32_t)take; data += take; n -= take;
      if (buf_len == 64) { process(buf); buf_len = 0; }
    }
  }

  void finalize(uint8_t* out, size_t out_len) {
    uint64_t bits = total_len * 8;
    buf[buf_len++] = 0x80;
    if (buf_len > 56) { while (buf_len < 64) buf[buf_len++] = 0; process(buf); buf_len = 0; }
    while (buf_len < 56) buf[buf_len++] = 0;
    for (int i = 0; i < 8; i++) buf[56 + i] = (uint8_t)(bits >> (56 - 8*i));
    process(buf); buf_len = 0;
    uint8_t full[32];
    for (int i = 0; i < 8; i++) {
      full[i*4]   = (uint8_t)(h[i] >> 24); full[i*4+1] = (uint8_t)(h[i] >> 16);
      full[i*4+2] = (uint8_t)(h[i] >> 8);  full[i*4+3] = (uint8_t)(h[i]);
    }
    if (out_len > 32) out_len = 32;
    memcpy(out, full, out_len);
  }

  // Standard HMAC-SHA256 (kept correct for API parity; OTA tests don't exercise it).
  void resetHMAC(const uint8_t* key, size_t keyLen) {
    memset(hmac_key, 0, 64);
    if (keyLen > 64) { SHA256 t; t.update(key, keyLen); t.finalize(hmac_key, 32); }
    else memcpy(hmac_key, key, keyLen);
    reset();
    uint8_t ipad[64];
    for (int i = 0; i < 64; i++) ipad[i] = hmac_key[i] ^ 0x36;
    update(ipad, 64);
  }

  void finalizeHMAC(const uint8_t* key, size_t keyLen, uint8_t* out, size_t out_len) {
    (void)key; (void)keyLen;
    uint8_t inner[32]; finalize(inner, 32);
    uint8_t opad[64];
    for (int i = 0; i < 64; i++) opad[i] = hmac_key[i] ^ 0x5c;
    reset(); update(opad, 64); update(inner, 32); finalize(out, out_len);
  }
};
