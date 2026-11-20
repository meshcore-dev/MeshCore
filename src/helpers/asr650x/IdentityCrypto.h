#pragma once
#include <stddef.h>

// Ed25519 on the big stack (BigStack.h): the software Ed25519 needs more than the ASR650x's fixed 2 KB main
// stack. Identity.cpp includes this header when IDENTITY_CRYPTO_ON_BIG_STACK is defined; the macros below
// then route its ed25519_* calls through these wrappers.
void asr650xEd25519CreateKeypair(unsigned char *pub, unsigned char *prv, const unsigned char *seed);
void asr650xEd25519DerivePub(unsigned char *pub, const unsigned char *prv);
void asr650xEd25519Sign(unsigned char *sig, const unsigned char *msg, size_t len, const unsigned char *pub,
                        const unsigned char *prv);
void asr650xEd25519KeyExchange(unsigned char *secret, const unsigned char *pub, const unsigned char *prv);
bool asr650xEd25519Verify(const unsigned char *sig, const unsigned char *pub, const unsigned char *msg,
                          size_t len);

#ifndef ASR650X_IDENTITY_CRYPTO_IMPL
#define ed25519_create_keypair asr650xEd25519CreateKeypair
#define ed25519_derive_pub     asr650xEd25519DerivePub
#define ed25519_sign           asr650xEd25519Sign
#define ed25519_key_exchange   asr650xEd25519KeyExchange
#endif
