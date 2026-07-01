#include "MotaContainer.h"
#include "MerkleTree.h"
#include "Multihash.h"
#include "OtaByteIO.h"
#include <string.h>

namespace mesh {
namespace ota {

bool MotaManifest::is_approved() const {
  return approval && memcmp(approval, APPROVAL_YES, 4) == 0;
}

// Fixed-layout parse (docs/ota_protocol.md §4): every field sits at a constant offset — base_hash(8),
// signer_pubkey(32) and signature(64) are ALWAYS present (zero-filled when not applicable), so there are
// no conditionals. Only leaves[]/payload (after `approval`) is variable, read by the caller. The signature
// always covers manifest[0, MOTA_SIGNED_LEN). Returns false on any over-read or bad format_ver.
static bool parse_manifest_fields(ByteReader& r, MotaManifest& out) {
  out.format_ver = r.u8();
  if (out.format_ver != MOTA_FORMAT_VER) return false;
  out.flags          = r.u8();
  out.hash_algo      = r.u8();
  out.target_id      = r.u32();
  out.fw_version     = r.u32();
  out.image_size     = r.u32();
  out.payload_size   = r.u32();
  out.block_size_log2 = r.u8();
  out.merkle_root    = r.take(4);
  out.image_hash     = r.take(32);
  out.codec_id       = r.u8();
  out.hw_id          = r.take(32);              // 32-byte NUL-padded hardware tag (signed)
  out.base_hash      = r.take(8);               // always present (zero for a full image)
  out.signer_pubkey  = r.take(32);              // always present (zero when unsigned)
  out.signed_len     = MOTA_SIGNED_LEN;         // signature always covers manifest[0, 129)
  out.signature      = r.take(64);              // always present (zero when unsigned)
  out.approval       = r.take(4);
  if (!r.ok) return false;
  if (out.block_size_log2 == 0 || out.block_size_log2 > 24 || out.payload_size == 0) return false;
  out.block_count = (out.payload_size + out.block_size() - 1) / out.block_size();
  // block_idx is uint16 on the wire; capping here also keeps block_count*4 (leaves length) from overflowing.
  return out.block_count != 0 && out.block_count <= 0xFFFFu;
}

bool mota_parse(const uint8_t* buf, uint32_t len, MotaManifest& out) {
  out = MotaManifest();
  if (len < 4 + 4 + 5) return false;
  if (memcmp(buf, MOTA_MAGIC, 4) != 0) return false;
  if (memcmp(buf + len - 5, MOTA_TRAILER, 5) != 0) return false;
  if (rd_u32le(buf + 4) != len) return false;   // MOTA_TOTAL_SIZE must equal the actual length

  ByteReader r(buf, len - 5);                   // everything up to (not incl.) the trailer
  r.skip(4 + 4);                                // MAGIC + MOTA_TOTAL_SIZE (already validated)
  out.manifest_start = buf + 8;
  if (!parse_manifest_fields(r, out)) return false;
  out.leaves  = r.take(out.block_count * 4);
  out.payload = r.take(out.payload_size);
  if (!r.ok) return false;
  return r.pos() == len - 5;                     // payload must end exactly at the trailer
}

bool mota_parse_manifest(const uint8_t* mf, uint32_t len, MotaManifest& out) {
  out = MotaManifest();
  out.manifest_start = mf;
  ByteReader r(mf, len);                         // a standalone manifest = container bytes [8, leaves)
  return parse_manifest_fields(r, out);
}

bool mota_check_root(const MotaManifest& m) {
  if (!m.leaves || m.block_count == 0) return false;
  uint8_t root[4];
  merkle_root(root, m.leaves, m.block_count);
  return memcmp(root, m.merkle_root, 4) == 0;
}

bool mota_check_image_hash_full(const MotaManifest& m) {
  if (!m.is_full() || !m.payload || !m.image_hash) return false;
  uint8_t h[32];
  mh32(h, m.payload, m.payload_size);
  return memcmp(h, m.image_hash, 32) == 0;
}

} // namespace ota
} // namespace mesh
