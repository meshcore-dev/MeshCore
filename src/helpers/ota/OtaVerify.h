#pragma once

#include "MotaContainer.h"
#include "SignerAllowlist.h"

// Full verification of a staged `.mota` (device-side: uses Ed25519 via mesh::Identity, so NOT compiled
// on the native host — the portable integrity checks live in MotaContainer and are unit-tested there).

namespace mesh {
namespace ota {

struct VerifyResult {
  bool parsed = false;     // container + manifest parsed
  bool root_ok = false;    // merkle_root recomputed from leaves[] matches
  bool image_ok = false;   // full: sha2-256(payload)==image_hash; delta: deferred to apply (set true)
  bool is_signed = false;
  bool sig_ok = false;     // Ed25519 signature valid for signer_pubkey
  bool trusted = false;    // signer_pubkey is in the allowlist

  // Integrity holds (safe to keep/serve). For a signed image, the signature must also verify.
  bool integrity_ok() const { return parsed && root_ok && image_ok && (!is_signed || sig_ok); }
  // Eligible for AUTO-apply: integrity + signed by an allowlisted key (decision D2).
  bool auto_appliable() const { return integrity_ok() && is_signed && sig_ok && trusted; }
};

VerifyResult ota_verify(const uint8_t* buf, uint32_t len, const SignerAllowlist& allow);

} // namespace ota
} // namespace mesh
