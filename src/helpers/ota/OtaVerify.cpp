#include "OtaVerify.h"
#include "MerkleTree.h"
#include "Multihash.h"
#include "Identity.h"

namespace mesh {
namespace ota {

VerifyResult ota_verify(const uint8_t* buf, uint32_t len, const SignerAllowlist& allow) {
  VerifyResult r;
  MotaManifest m;
  if (!mota_parse(buf, len, m)) return r;
  r.parsed = true;
  r.root_ok = mota_check_root(m);
  r.image_ok = m.is_full() ? mota_check_image_hash_full(m)
                           : true;   // delta image_hash needs the base; verified at apply time
  r.is_signed = m.is_signed();
  if (r.is_signed) {
    mesh::Identity signer(m.signer_pubkey);
    r.sig_ok = signer.verify(m.signature, m.manifest_start, (int)m.signed_len);
    r.trusted = r.sig_ok && allow.contains(m.signer_pubkey);
  }
  return r;
}

} // namespace ota
} // namespace mesh
