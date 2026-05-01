#ifndef PERMUTATIONS_H_
#define PERMUTATIONS_H_

#include <stdint.h>

#include "api.h"
#include "ascon.h"
#include "config.h"
#include "constants.h"
#include "printstate.h"
#include "round.h"

forceinline void P12ROUNDS(ascon_state_t* s) {
  ROUND(s, RC0);
  ROUND(s, RC1);
  ROUND(s, RC2);
  ROUND(s, RC3);
  ROUND(s, RC4);
  ROUND(s, RC5);
  ROUND(s, RC6);
  ROUND(s, RC7);
  ROUND(s, RC8);
  ROUND(s, RC9);
  ROUND(s, RCa);
  ROUND(s, RCb);
}

forceinline void P8ROUNDS(ascon_state_t* s) {
  ROUND(s, RC4);
  ROUND(s, RC5);
  ROUND(s, RC6);
  ROUND(s, RC7);
  ROUND(s, RC8);
  ROUND(s, RC9);
  ROUND(s, RCa);
  ROUND(s, RCb);
}

forceinline void P6ROUNDS(ascon_state_t* s) {
  ROUND(s, RC6);
  ROUND(s, RC7);
  ROUND(s, RC8);
  ROUND(s, RC9);
  ROUND(s, RCa);
  ROUND(s, RCb);
}

#if ASCON_INLINE_PERM && ASCON_UNROLL_LOOPS

forceinline void P(ascon_state_t* s, int nr) {
  if (nr == 12) P12ROUNDS(s);
  if (nr == 8) P8ROUNDS(s);
  if (nr == 6) P6ROUNDS(s);
}

#elif !ASCON_INLINE_PERM && ASCON_UNROLL_LOOPS

void P12(ascon_state_t* s);
void P8(ascon_state_t* s);
void P6(ascon_state_t* s);

forceinline void P(ascon_state_t* s, int nr) {
  if (nr == 12) P12(s);
#if ((defined(ASCON_AEAD_RATE) && ASCON_AEAD_RATE == 16) ||    \
     (defined(ASCON_HASH_ROUNDS) && ASCON_HASH_ROUNDS == 8) || \
     (defined(ASCON_PRF_ROUNDS) && ASCON_PRF_ROUNDS == 8))
  if (nr == 8) P8(s);
#endif
#if (defined(ASCON_AEAD_RATE) && ASCON_AEAD_RATE == 8)
  if (nr == 6) P6(s);
#endif
}

#elif ASCON_INLINE_PERM && !ASCON_UNROLL_LOOPS

forceinline void P(ascon_state_t* s, int nr) { PROUNDS(s, nr); }

#else /* !ASCON_INLINE_PERM && !ASCON_UNROLL_LOOPS */

void P(ascon_state_t* s, int nr);

#endif

#endif /* PERMUTATIONS_H_ */
