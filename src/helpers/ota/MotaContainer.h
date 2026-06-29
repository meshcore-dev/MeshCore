#pragma once

#include <stdint.h>
#include <stddef.h>
#include "OtaFormat.h"

// Parse/validate a `.mota` container that is fully present in a RAM buffer (docs/ota_protocol.md
// §3-§4). Variable-length parts are referenced by pointer into the caller's buffer — no copies, no
// allocation. (Device flash-backed staging gets a streaming variant in a later milestone; the field
// layout here is the single source of truth.)

namespace mesh {
namespace ota {

struct MotaManifest {
  uint8_t  format_ver = 0;
  uint8_t  flags = 0;
  uint8_t  hash_algo = 0;
  uint32_t target_id = 0;
  uint32_t fw_version = 0;
  uint32_t image_size = 0;
  uint32_t payload_size = 0;
  uint8_t  block_size_log2 = 0;
  uint8_t  codec_id = 0;
  uint32_t block_count = 0;

  // Fixed layout (docs/ota_protocol.md §4): every field below sits at a constant offset and is ALWAYS
  // present; base_hash/signer_pubkey/signature are zero-filled when not applicable (full / unsigned).
  const uint8_t* merkle_root = nullptr;   // 4  @20
  const uint8_t* image_hash = nullptr;    // 32 @24
  const uint8_t* hw_id = nullptr;         // 32 @57 (NUL-padded ASCII hardware tag; signed)
  const uint8_t* base_hash = nullptr;     // 8  @89 (zero for a full image)
  const uint8_t* signer_pubkey = nullptr; // 32 @97 (zero when unsigned)
  const uint8_t* signature = nullptr;     // 64 @129 (zero when unsigned)
  const uint8_t* approval = nullptr;      // 4  @193
  const uint8_t* leaves = nullptr;        // 4 * block_count (the only variable-length field)
  const uint8_t* payload = nullptr;       // payload_size
  const uint8_t* manifest_start = nullptr;// first manifest byte (== start of the signed region)
  uint32_t signed_len = 0;                // = MOTA_SIGNED_LEN (129): bytes the signature covers

  bool is_full()   const { return flags & MFLAG_FULL; }
  bool is_signed() const { return flags & MFLAG_SIGNED; }
  uint32_t block_size() const { return 1u << block_size_log2; }
  bool is_approved() const;
};

// Parse a whole container in `buf[len]`. Returns true on success and fills `out` with pointers into
// `buf`. Validates MAGIC, TRAILER, MOTA_TOTAL_SIZE, format_ver, and internal length consistency.
bool mota_parse(const uint8_t* buf, uint32_t len, MotaManifest& out);

// Parse a standalone manifest (the bytes [manifest_start, leaves) of a container, i.e. without the
// MAGIC/TOTAL_SIZE framing, leaves[] or payload). Used by the apply path, which receives the manifest
// separately from the image. Sets the fixed fields + signer/signature + signed_len; leaves/payload
// are left null.
bool mota_parse_manifest(const uint8_t* mf, uint32_t len, MotaManifest& out);

// Recompute the merkle root from the manifest's leaves[] and compare to the merkle_root field.
bool mota_check_root(const MotaManifest& m);

// For FULL images only: check sha2-256:32(payload) == image_hash.
bool mota_check_image_hash_full(const MotaManifest& m);

} // namespace ota
} // namespace mesh
