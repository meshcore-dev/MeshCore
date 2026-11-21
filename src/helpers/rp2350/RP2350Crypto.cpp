// RP2350 hardware SHA-256 (see RP2350Crypto.h).
// Only compiled when the env enables it (rp2350_base defines USE_RP2350_HW_SHA256),
// so globbing this directory into other builds is safe.
#ifdef USE_RP2350_HW_SHA256

#include "RP2350Crypto.h"

#include <string.h>

#include <pico/error.h>
#include <pico/time.h>
#include <pico/sha256.h>

namespace rp2350 {

// The accelerator is exclusive and call sites (advert receive, key derivation)
// are short-lived, so a static state suffices; same pattern as the CC310
// contexts in Utils.cpp.
static pico_sha256_state_t s_state;

static bool run(uint8_t* hash, size_t hash_len,
                const uint8_t* frag1, int frag1_len,
                const uint8_t* frag2, int frag2_len) {
  // Give the accelerator up to ~1 ms before giving up; on failure the caller
  // falls back to software SHA-256.
  absolute_time_t until = make_timeout_time_us(1000);
  if (pico_sha256_start_blocking_until(&s_state, SHA256_BIG_ENDIAN, false, until) != PICO_OK) {
    return false;
  }

  // use_dma=false: mesh-sized payloads don't need DMA offload, and this keeps
  // the call self-contained (no DMA channel consumed).
  if (frag1_len > 0) pico_sha256_update_blocking(&s_state, frag1, (size_t)frag1_len);
  if (frag2_len > 0) pico_sha256_update_blocking(&s_state, frag2, (size_t)frag2_len);

  sha256_result_t result;
  pico_sha256_finish(&s_state, &result);

  size_t copy_len = hash_len < SHA256_RESULT_BYTES ? hash_len : SHA256_RESULT_BYTES;
  memcpy(hash, result.bytes, copy_len);
  return true;
}

bool hwSha256(uint8_t* hash, size_t hash_len, const uint8_t* msg, int msg_len) {
  return run(hash, hash_len, msg, msg_len, nullptr, 0);
}

bool hwSha256(uint8_t* hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len) {
  return run(hash, hash_len, frag1, frag1_len, frag2, frag2_len);
}

}  // namespace rp2350

#endif  // USE_RP2350_HW_SHA256
