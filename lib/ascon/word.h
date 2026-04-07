#ifndef WORD_H_
#define WORD_H_

#include <stdint.h>
#include <string.h>

#include "forceinline.h"
#include "lendian.h"

typedef union {
  uint64_t x;
  uint32_t w[2];
  uint8_t b[8];
} word_t;

#define U64TOWORD(x) U64LE(x)
#define WORDTOU64(x) U64LE(x)
#define LOAD(b, n) LOADBYTES(b, n)
#define STORE(b, w, n) STOREBYTES(b, w, n)

forceinline uint64_t ROR(uint64_t x, int n) { return x >> n | x << (-n & 63); }

forceinline uint64_t KEYROT(uint64_t hi2lo, uint64_t lo2hi) {
  return lo2hi << 32 | hi2lo >> 32;
}

forceinline int NOTZERO(uint64_t a, uint64_t b) {
  uint64_t result = a | b;
  result |= result >> 32;
  result |= result >> 16;
  result |= result >> 8;
  return ((((int)(result & 0xff) - 1) >> 8) & 1) - 1;
}

forceinline uint64_t PAD(int i) { return 0x01ull << (8 * i); }

forceinline uint64_t DSEP() { return 0x80ull << 56; }

forceinline uint64_t PRFS_MLEN(uint64_t len) { return len << 51; }

forceinline uint64_t CLEAR(uint64_t w, int n) {
  /* undefined for n == 0 */
  uint64_t mask = ~0ull << (8 * n);
  return w & mask;
}

forceinline uint64_t MASK(int n) {
  /* undefined for n == 0 */
  return ~0ull << (64 - 8 * n);
}

forceinline uint64_t LOADBYTES(const uint8_t* bytes, int n) {
  uint64_t x = 0;
  memcpy(&x, bytes, n);
  return U64TOWORD(x);
}

forceinline void STOREBYTES(uint8_t* bytes, uint64_t w, int n) {
  uint64_t x = WORDTOU64(w);
  memcpy(bytes, &x, n);
}

#endif /* WORD_H_ */
