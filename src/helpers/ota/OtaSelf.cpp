#include "OtaSelf.h"
#include "FirmwareInfo.h"
#include "OtaByteIO.h"
#include <string.h>

#if defined(ESP32_PLATFORM)
  #include "esp_ota_ops.h"
  #include "esp_partition.h"
#elif defined(NRF52_PLATFORM)
  #include "OtaFlashLayout_nrf52.h"
#endif

#if defined(ESP32_PLATFORM) || defined(NRF52_PLATFORM)
  #include "OtaContext.h"     // serve our own fw from flash (cache leaves, read payload on demand)
  #include "MerkleTree.h"
  #include <SHA256.h>
  #include <stdlib.h>
  #ifndef OTA_SELF_LEAVES_MAX
  #define OTA_SELF_LEAVES_MAX 65536u   // cap heap for cached leaves (~16k blocks @1 KB = up to ~16 MB image)
  #endif
#endif

namespace mesh {
namespace ota {

#if defined(ESP32_PLATFORM)
// Scan the running app partition for the firmware's EndF trailer using esp_partition_read (stable
// across IDF versions — no mmap). Same rule as find_self_firmware(): the marker's absolute offset
// must equal its stored body_len, which uniquely identifies the running firmware's own trailer.
bool ota_self_firmware(SelfFwInfo& out) {
  out = SelfFwInfo();
  const esp_partition_t* p = esp_ota_get_running_partition();
  if (!p) return false;

  const uint32_t CH = 512;
  uint8_t buf[CH + ENDF_LEN];                 // overlap so a marker spanning a chunk edge is still seen
  for (uint32_t base = 0; base + ENDF_LEN <= p->size; base += CH) {
    uint32_t want = CH + ENDF_LEN;
    if (base + want > p->size) want = p->size - base;
    if (esp_partition_read(p, base, buf, want) != ESP_OK) return false;
    for (uint32_t i = 0; i + ENDF_LEN <= want; i++) {
      if (buf[i] != ENDF_MAGIC[0]) continue;
      if (memcmp(buf + i, ENDF_MAGIC, 4) != 0) continue;
      uint32_t body_len = rd_u32le(buf + i + 4);
      if (body_len != base + i) continue;     // must sit immediately after a body of that length
      out.valid = true;
      out.endf_offset = body_len;
      out.body_len = body_len;
      out.image_len = body_len + ENDF_LEN;
      memcpy(out.body_hash, buf + i + 8, 8);
      // Fixed 56-byte trailer: re-read it whole at the marker (it may straddle the chunk window, so the
      // identity fields aren't reliably in `buf`) and pull identity from constant offsets (docs §2).
      uint8_t tr[ENDF_LEN];
      if (body_len + ENDF_LEN <= p->size &&
          esp_partition_read(p, body_len, tr, ENDF_LEN) == ESP_OK) {
        out.fw_version = (uint32_t)tr[16] | ((uint32_t)tr[17]<<8) | ((uint32_t)tr[18]<<16) | ((uint32_t)tr[19]<<24);
        out.target_id  = (uint32_t)tr[20] | ((uint32_t)tr[21]<<8) | ((uint32_t)tr[22]<<16) | ((uint32_t)tr[23]<<24);
        memcpy(out.hw_id, tr + 24, 32); out.hw_id[32] = 0;
      }
      return true;
    }
  }
  return false;
}
#elif defined(NRF52_PLATFORM)
// nRF52 internal flash is memory-mapped, so the running app is directly scannable. The body starts at
// APP_BASE; find_self_firmware() picks the EndF whose stored body_len equals its offset (the running
// firmware's own trailer), ignoring any staged `.mota` (which carries its own embedded EndF) higher up.
bool ota_self_firmware(SelfFwInfo& out) {
  const uint8_t* region = (const uint8_t*)(uintptr_t)MOTA_NRF52_APP_BASE;
  uint32_t region_len = MOTA_NRF52_FS_START - MOTA_NRF52_APP_BASE;
  return find_self_firmware(region, region_len, out, /*verify_body=*/true);
}
#else
bool ota_self_firmware(SelfFwInfo& out) {
  // STM32/RP2040: app-region access lands with their apply path.
  out = SelfFwInfo();
  return false;
}
#endif

#if defined(ESP32_PLATFORM)
bool ota_self_read(uint32_t off, uint8_t* buf, uint32_t len) {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p && esp_partition_read(p, off, buf, len) == ESP_OK;
}
#elif defined(NRF52_PLATFORM)
bool ota_self_read(uint32_t off, uint8_t* buf, uint32_t len) {
  if ((uint64_t)MOTA_NRF52_APP_BASE + off + len > MOTA_NRF52_FS_START) return false;
  memcpy(buf, (const uint8_t*)(uintptr_t)(MOTA_NRF52_APP_BASE + off), len);
  return true;
}
#else
bool ota_self_read(uint32_t, uint8_t*, uint32_t) { return false; }
#endif

#if defined(ESP32_PLATFORM) || defined(NRF52_PLATFORM)
static bool self_read_cb(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
  (void)ctx; return ota_self_read(off, buf, len);
}
// Build (once) the full-image manifest + merkle leaves for the running firmware, cache them in `c`, and
// hand the manager a flash-read callback for the payload. The image is read ONCE here to compute the
// leaves + image_hash; thereafter a block REQ reads only that block (proof comes from the cached leaves).
// Pack the first "MAJOR.MINOR.PATCH" found in `s` into the comparable uint32 the manifest uses
// (MAJOR<<24 | MINOR<<16 | PATCH<<8). Returns 0 if there's no dotted number (e.g. a "dev-<sha>" build).
static uint32_t parse_fw_version(const char* s) {
  if (!s) return 0;
  for (; *s; s++) {                                   // find the start of a "d.d" run
    if (*s < '0' || *s > '9') continue;
    const char* p = s; uint32_t a = 0, b = 0, d = 0; int dots = 0;
    uint32_t* cur = &a;
    for (; *p; p++) {
      if (*p >= '0' && *p <= '9') { *cur = *cur * 10 + (uint32_t)(*p - '0'); }
      else if (*p == '.' && dots < 2) { dots++; cur = (dots == 1) ? &b : &d; }
      else break;
    }
    if (dots >= 1) return FwVersion{ (uint8_t)a, (uint8_t)b, (uint8_t)d, 0 }.pack();
    s = p - 1;                                        // a bare number, no dots — keep scanning
  }
  return 0;
}

bool ota_serve_self(OtaContext& c, uint32_t fw_version) {
  // Derive our version from the build string when the caller didn't supply one, so the mOTA we advertise
  // carries a real version (was hard-coded 0 -> peers saw "v0.0.0"). A dev build with no dotted number
  // still reads 0 — the self-describing EndF identity (docs) is the durable fix for that.
#ifdef FIRMWARE_VERSION
  if (fw_version == 0) fw_version = parse_fw_version(FIRMWARE_VERSION);
#endif
  SelfFwInfo fi;
  if (!ota_self_firmware(fi) || !fi.valid) return false;
  // 1 KB logical blocks (delivered as multiple LoRa fragments): 8x fewer merkle leaves than 128 B, so a
  // ~530 KB image is ~518 blocks (proof-gen scratch ~2 KB) instead of ~4150 (which overflowed the scratch).
  const uint32_t image_size = fi.image_len, BS = OTA_DEFAULT_BLOCK_SIZE;
  const uint32_t bc = (image_size + BS - 1) / BS;
  if ((uint64_t)bc * 4 > OTA_SELF_LEAVES_MAX) return false;

  free(c.serve_self_leaves); free(c.serve_self_proof);
  c.serve_self_leaves = (uint8_t*)malloc((size_t)bc * 4);
  c.serve_self_proof  = (uint8_t*)malloc((size_t)bc * 4);   // proof-gen working buffer (sized to OUR image)
  if (!c.serve_self_leaves || !c.serve_self_proof) {
    free(c.serve_self_leaves); free(c.serve_self_proof);
    c.serve_self_leaves = c.serve_self_proof = nullptr;
    return false;
  }

  SHA256 sha; uint8_t blk[BS];
  for (uint32_t i = 0, off = 0; i < bc; i++, off += BS) {
    uint32_t blen = (off + BS <= image_size) ? BS : (image_size - off);
    if (!ota_self_read(off, blk, blen)) {
      free(c.serve_self_leaves); free(c.serve_self_proof);
      c.serve_self_leaves = c.serve_self_proof = nullptr;
      return false;
    }
    merkle_leaf(c.serve_self_leaves + (size_t)i * 4, blk, blen);
    sha.update(blk, blen);
  }
  uint8_t image_hash[32]; sha.finalize(image_hash, 32);
  uint8_t root[4]; merkle_root(root, c.serve_self_leaves, bc);

  // Prefer the SELF-DESCRIBING identity embedded in our own EndF (docs §2) over build flags / the param —
  // it's correct regardless of how the firmware was built (build.sh injection, IDE, etc.).
  uint32_t out_target = fi.target_id ? fi.target_id : c.manager.target();
  uint32_t out_ver    = fi.fw_version ? fi.fw_version : fw_version;
  const char* out_hw  = fi.hw_id[0] ? fi.hw_id : c.hw_id;

  // Assemble the fixed-layout manifest-minus-leaves (full, unsigned) = MOTA_MFL bytes. base_hash(89),
  // signer_pubkey(97) and signature(129) stay zero-filled (full + unsigned); only `approval` is set.
  uint8_t* m = c.serve_self_manifest;
  memset(m, 0, MOTA_MFL);
  m[0] = MOTA_FORMAT_VER; m[1] = MFLAG_FULL; m[2] = HASH_ALGO_SHA256;
  wr_u32le(m + 3, out_target); wr_u32le(m + 7, out_ver);
  wr_u32le(m + 11, image_size); wr_u32le(m + 15, image_size);   // full: payload == image
  m[19] = 10;                                 // block_size_log2 = 10 (1024 B logical block)
  memcpy(m + 20, root, 4);
  memcpy(m + 24, image_hash, 32);
  m[56] = CODEC_FULL;
  memcpy(m + 57, out_hw, strlen(out_hw) < 32 ? strlen(out_hw) : 32);   // hw_id[32] (NUL-padded by memset)
  memcpy(m + MOTA_OFF_APPROVAL, APPROVAL_NOT, 4);   // approval (fetching device's apply-gate handles it)
  return c.manager.serve_self(m, MOTA_MFL, c.serve_self_leaves, bc,
                              c.serve_self_proof, (size_t)bc * 4, self_read_cb, nullptr);
}
#endif

} // namespace ota
} // namespace mesh
