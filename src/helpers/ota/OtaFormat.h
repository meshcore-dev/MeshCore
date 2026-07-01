#pragma once

#include <stdint.h>
#include <stddef.h>

// On-the-wire constants for the MeshCore OTA `.mota` container and protocol.
// Normative definition: docs/ota_protocol.md (v1). Mirrors tools/mota/motalib.py.
//
// Portable: no Arduino / RadioLib includes. Compiles on the native host (unit tests) and on device.

namespace mesh {
namespace ota {

// ---- container framing ----------------------------------------------------
static const uint8_t  MOTA_MAGIC[4]    = { 'm', 'O', 'T', 'A' };   // 6D 4F 54 41
static const uint8_t  MOTA_TRAILER[5]  = { 'v', 'k', '4', '9', '6' }; // 76 6B 34 39 36
static const uint8_t  ENDF_MAGIC[4]    = { 'E', 'n', 'd', 'F' };   // 45 6E 64 46
// Fixed 56-byte trailer (docs/ota_protocol.md §2): marker(4) body_len(4) body_hash8(8) + a self-describing
// identity block fw_version(4) target_id(4) hw_id(32). No optional/variable parts.
static const uint32_t ENDF_LEN         = 56;

// ---- manifest -------------------------------------------------------------
static const uint8_t  MOTA_FORMAT_VER  = 2;      // fixed-layout manifest (see offsets below)
static const uint8_t  HASH_ALGO_SHA256 = 0x12;   // multihash code

// Fixed manifest layout (manifest-minus-leaves) — every field is present at a constant offset, so the
// parser is plain offset reads (docs/ota_protocol.md §4). base_hash/signer_pubkey/signature are always
// present (zero-filled when not applicable); only leaves[] (after `approval`) is variable.
static const uint32_t MOTA_OFF_BASE_HASH = 89;   // 8  (zero for a full image)
static const uint32_t MOTA_OFF_SIGNER    = 97;   // 32 (zero when unsigned)
static const uint32_t MOTA_OFF_SIGNATURE = 129;  // 64 (zero when unsigned) — covers manifest[0,129)
static const uint32_t MOTA_OFF_APPROVAL  = 193;  // 4
static const uint32_t MOTA_MFL           = 197;  // manifest-minus-leaves length (constant)
static const uint32_t MOTA_SIGNED_LEN    = 129;  // bytes the signature covers (manifest[0, signer_end))

// hw_id: a fixed 32-byte, NUL-padded ASCII string naming the hardware a firmware can boot on (e.g.
// "RAK4631", "Heltec_v3"). Same hw_id == bootable-compatible (a role switch on the same board keeps it;
// different MCU/board differs). It sits in the SIGNED region of the manifest, so it can't be tampered.
// The applier refuses a `.mota` whose hw_id differs from the device's own (brick-safety, esp. for a manual
// cross-target `ota dev want`). An empty hw_id on either side = "unknown", and the check is skipped.
static const uint8_t  MOTA_HW_ID_LEN   = 32;

static const uint8_t  MFLAG_FULL       = 0x01;   // 0 = delta/partial, 1 = full image
static const uint8_t  MFLAG_SIGNED     = 0x02;

static const uint8_t  CODEC_FULL                 = 0;
static const uint8_t  CODEC_DETOOLS_SEQUENTIAL   = 1;
static const uint8_t  CODEC_DETOOLS_INPLACE      = 2;

// ---- firmware version ------------------------------------------------------
// The comparable uint32 carried in the manifest + EndF identity: MAJOR<<24 | MINOR<<16 | PATCH<<8 | PRE.
// 0 == "unknown" (e.g. a dev build with no dotted version). Defined once here so the pack/unpack layout
// isn't re-derived with raw shifts at each call site (OtaSelf builds it, OtaCli renders it).
struct FwVersion {
  uint8_t major, minor, patch, prerelease;
  static FwVersion unpack(uint32_t v) {
    return { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
  }
  uint32_t pack() const {
    return ((uint32_t)major << 24) | ((uint32_t)minor << 16) | ((uint32_t)patch << 8) | prerelease;
  }
};

// ---- hash truncations -----------------------------------------------------
static const uint8_t  MH4  = 4;    // sha2-256:4  (merkle leaves/nodes/root/proofs)
static const uint8_t  MH8  = 8;    // sha2-256:8  (base/EndF body hash)
static const uint8_t  MH32 = 32;   // sha2-256:32 (image security anchor)

// ---- approval marker (manifest field, after the signature) ----------------
static const uint8_t  APPROVAL_NOT[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t  APPROVAL_YES[4] = { 'A', 'P', 'R', 'V' };   // 41 50 52 56

// ---- LoRa protocol --------------------------------------------------------
// The packet payload type is PAYLOAD_TYPE_OTA (0x0C), defined in src/Packet.h for the core dispatch.

enum OtaMsgType : uint8_t {
  OTA_ADV          = 0x01,
  OTA_QUERY        = 0x02,
  OTA_HAVE         = 0x03,
  OTA_GET_MANIFEST = 0x04,
  OTA_MANIFEST     = 0x05,
  OTA_REQ          = 0x06,   // request a window of blocks' DATA fragments
  OTA_DATA         = 0x07,   // one fragment of a block's data (self-describing by frag_off; no proof)
  OTA_REQ_PROOF    = 0x08,   // request the merkle proof for one block (data + proof are fetched separately)
  OTA_PROOF        = 0x09,   // the merkle proof for one block
};

static const uint16_t OTA_DEFAULT_BLOCK_SIZE = 1024;
static const uint8_t  OTA_DEFAULT_HOP_LIMIT  = 3;

} // namespace ota
} // namespace mesh
