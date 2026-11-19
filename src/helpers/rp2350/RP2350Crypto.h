#pragma once

// RP2350 (Pico 2) hardware crypto helpers.
//
// The RP2350 adds a hardware SHA-256 accelerator on top of the RP2040; it is
// the only crypto peripheral driver in the pico-sdk build the arduino-pico
// framework ships. AES / HMAC / Ed25519 remain software (rweather/Crypto);
// see the STUB notes at the bottom.

#include <stdint.h>
#include <stddef.h>

namespace rp2350 {

/**
 * Hash 'msg' using the hardware SHA-256 accelerator.
 *
 * The accelerator is exclusive (one calculation at a time). If it is busy,
 * this function returns false without touching 'hash', so the caller can
 * fall back to the software implementation.
 *
 * @param hash      destination for the digest
 * @param hash_len  number of digest bytes to store (<= 32)
 * @param msg       data to hash
 * @param msg_len   length of 'msg' in bytes
 * @returns true if the hardware produced the digest, false if it was busy.
 */
bool hwSha256(uint8_t* hash, size_t hash_len, const uint8_t* msg, int msg_len);

/**
 * Hash two fragments (frag1 followed by frag2) using the hardware SHA-256 accelerator.
 * Same contract as hwSha256() above.
 */
bool hwSha256(uint8_t* hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len);

// ---------------------------------------------------------------------------
// STUB: hardware AES
//
// The RP2350 also has an AES-128/256 accelerator (upstream pico-sdk
// "hardware_crypto" driver), but the pico-sdk build the arduino-pico framework
// ships does not include it. To enable later:
//   1. vendor/upgrade the SDK so src/common/hardware_crypto is present,
//   2. link the hardware_crypto library into the firmware,
//   3. implement hwAesBlock() here and add a USE_RP2350_HW_AES branch in
//      Utils.cpp mirroring the USE_CC310_HW_CRYPTO encrypt/decrypt paths.
// Until then AES block cipher + HMAC-SHA256 + Ed25519 all run in software,
// which is the same situation as every non-CC310 ARM platform in this repo.
// ---------------------------------------------------------------------------

}  // namespace rp2350
