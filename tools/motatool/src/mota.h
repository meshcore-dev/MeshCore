// `.mota` container: parse, integrity-verify, and build (full / detools-delta). EndF identity helpers.
// Mirrors tools/mota/motalib.py and src/helpers/ota/MotaContainer.cpp (the single source of truth).
#pragma once
#include "mota_format.h"
#include <array>
#include <string>
#include <vector>

namespace mota {

struct FwIdent {
  uint32_t fw_version = 0;
  uint32_t target_id = 0;
  std::string hw_id;       // NUL-trimmed
  bool any() const { return fw_version || target_id || !hw_id.empty(); }
};

struct Manifest {
  uint8_t  format_ver = 0, flags = 0, hash_algo = 0, codec_id = 0, block_size_log2 = 0;
  uint32_t target_id = 0, fw_version = 0, image_size = 0, payload_size = 0, block_count = 0;
  std::array<uint8_t,4>  merkle_root{};
  std::array<uint8_t,32> image_hash{};
  std::array<uint8_t,32> hw_id{};       // raw 32 bytes (NUL-padded)
  std::array<uint8_t,8>  base_hash{};
  std::array<uint8_t,32> signer{};
  std::array<uint8_t,64> signature{};
  std::array<uint8_t,4>  approval{};

  bool is_full()   const { return flags & MFLAG_FULL; }
  bool is_signed() const { return flags & MFLAG_SIGNED; }
  uint32_t block_size()  const { return 1u << block_size_log2; }
  uint32_t leaves_off()  const { return 8 + MOTA_MFL; }
  uint32_t payload_off() const { return leaves_off() + block_count * 4; }
  uint32_t total_size()  const { return payload_off() + payload_size + 5; }
  std::string hw_id_str() const;
};

// Parse + validate framing and the fixed layout. Returns "" on success (fills `out`), else an error.
std::string parse(const std::vector<uint8_t>& blob, Manifest& out);

// Content-integrity check: recompute leaves[] from the payload vs merkle_root, the merkle root, the
// FULL image_hash, and (if signed) the Ed25519 signature against the embedded signer key. Returns a list
// of problems (empty => valid). A delta's image_hash is not checked here (it needs the base image).
std::vector<std::string> verify(const std::vector<uint8_t>& blob);

// ---- EndF identity (fixed 56-byte trailer) ----
bool has_endf(const std::vector<uint8_t>& image);
FwIdent parse_endf_ident(const std::vector<uint8_t>& image);   // zeros if no EndF
// Append a 56-byte EndF (with identity) if absent; returns the image and sets body_hash8. Idempotent.
std::vector<uint8_t> ensure_endf(const std::vector<uint8_t>& image, const FwIdent& id,
                                 std::array<uint8_t,8>& body_hash8);

uint32_t target_id_for_env(const std::string& env);     // sha2-256:4(env) as LE uint32
bool     pack_version(const std::string& s, uint32_t& out);   // "1.16.0[.pre]" -> packed uint32

// Reverse-lookup a target_id to its PlatformIO env name from a static table of known OTA-capable envs
// (target_id = sha2-256:4(env_name)). Returns "" if not in the table.
std::string target_env_name(uint32_t target_id);

// ---- build ----
struct BuildOpts {
  std::vector<uint8_t> fw;            // NEW firmware (raw or already-EndF'd)
  std::vector<uint8_t> base;          // base image for a delta (empty => full)
  uint8_t  codec = CODEC_FULL;        // CODEC_FULL / _SEQUENTIAL / _INPLACE
  bool     have_target = false; uint32_t target_id = 0;     // overrides EndF
  bool     have_fwver  = false; uint32_t fw_version = 0;    // overrides EndF
  std::string hw_id;                  // override; else from EndF
  std::vector<uint8_t> sign_priv;     // 32-byte raw private seed (empty => unsigned)
  uint32_t block_size = DEFAULT_BLOCK_SIZE;
  uint32_t inplace_memory = NRF52_INPLACE_MEMORY, inplace_segment = NRF52_INPLACE_SEGMENT;
  std::string detools = "detools";    // detools CLI path (delta encoding only)
  bool     force = false;             // override the cross-hardware delta guard
};
// Returns "" + fills `out` and a suggested file name on success; else an error string.
std::string build(const BuildOpts& o, std::vector<uint8_t>& out, std::string& suggested_name);

// Apply a SEQUENTIAL delta `patch` to `base_img` via the detools CLI -> reconstructed image in `out`.
// Used by `verify --base` to confirm a delta actually rebuilds the expected image (matches mota.py:
// in-place deltas aren't apply-checked here — the bootloader host-harness covers that path).
std::string detools_apply_seq(const std::string& detools, const std::vector<uint8_t>& base_img,
                              const std::vector<uint8_t>& patch, std::vector<uint8_t>& out);

} // namespace mota
