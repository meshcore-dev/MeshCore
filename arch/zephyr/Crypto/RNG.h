/*
 * Zephyr replacement for rweather/Crypto's RNG.h.
 *
 * Upstream RNG.cpp pulls in <Arduino.h>, AVR EEPROM and ESP-IDF NVS to run a
 * ChaCha-based PRNG with persistent seed storage. None of that ports, and
 * MeshCore never calls it: the only references are Ed25519::generatePrivateKey()
 * and Curve25519::dh1(), neither of which MeshCore uses (it generates keys via
 * lib/ed25519's ed25519_create_keypair, and only calls Ed25519::verify).
 *
 * Those two functions are still compiled, so the symbol has to exist. This
 * provides the one method they reference, backed by Zephyr's RNG, which keeps
 * them correct if anyone ever does call them.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CRYPTO_RNG_ZEPHYR_h
#define CRYPTO_RNG_ZEPHYR_h

#include <inttypes.h>
#include <stddef.h>

class RNGClass {
public:
  void rand(uint8_t *data, size_t len);
};

extern RNGClass RNG;

#endif
