#ifdef IDENTITY_CRYPTO_ON_BIG_STACK
#define ASR650X_IDENTITY_CRYPTO_IMPL
#include "IdentityCrypto.h"

#include "BigStack.h"
#define ED25519_NO_SEED 1
#include <Ed25519.h>
#include <ed_25519.h>

namespace {
struct Args {
  unsigned char *o1;
  const unsigned char *a, *b, *c;
  size_t len;
  bool ok;
};
void runKeypair(void *p) {
  Args *x = (Args *)p;
  ed25519_create_keypair(x->o1, (unsigned char *)x->a, x->b);
}
void runDerive(void *p) {
  Args *x = (Args *)p;
  ed25519_derive_pub(x->o1, x->a);
}
void runSign(void *p) {
  Args *x = (Args *)p;
  ed25519_sign(x->o1, x->a, x->len, x->b, x->c);
}
void runKeyex(void *p) {
  Args *x = (Args *)p;
  ed25519_key_exchange(x->o1, x->a, x->b);
}
void runVerify(void *p) {
  Args *x = (Args *)p;
  x->ok = Ed25519::verify(x->a, x->b, x->c, x->len);
}
} // namespace

void asr650xEd25519CreateKeypair(unsigned char *pub, unsigned char *prv, const unsigned char *seed) {
  Args a = { pub, prv, seed, 0, 0, false };
  asr650xCallOnBigStack(runKeypair, &a);
}
void asr650xEd25519DerivePub(unsigned char *pub, const unsigned char *prv) {
  Args a = { pub, prv, 0, 0, 0, false };
  asr650xCallOnBigStack(runDerive, &a);
}
void asr650xEd25519Sign(unsigned char *sig, const unsigned char *msg, size_t len, const unsigned char *pub,
                        const unsigned char *prv) {
  Args a = { sig, msg, pub, prv, len, false };
  asr650xCallOnBigStack(runSign, &a);
}
void asr650xEd25519KeyExchange(unsigned char *secret, const unsigned char *pub, const unsigned char *prv) {
  Args a = { secret, pub, prv, 0, 0, false };
  asr650xCallOnBigStack(runKeyex, &a);
}
bool asr650xEd25519Verify(const unsigned char *sig, const unsigned char *pub, const unsigned char *msg,
                          size_t len) {
  Args a = { 0, sig, pub, msg, len, false };
  asr650xCallOnBigStack(runVerify, &a);
  return a.ok;
}
#endif
