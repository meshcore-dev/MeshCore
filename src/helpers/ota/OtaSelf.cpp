#include "OtaSelf.h"
#include "OtaFormat.h"
#include "FirmwareInfo.h"
#include "OtaByteIO.h"
#include "OtaDebug.h"
#include <string.h>

#if defined(ESP32_PLATFORM)
  #include "esp_ota_ops.h"
  #include "esp_partition.h"
#elif defined(NRF52_PLATFORM)
  #include "OtaFlashLayout_nrf52.h"
#endif

#if OTA_SELF_SERVE && (defined(ESP32_PLATFORM) || defined(NRF52_PLATFORM))
  #include "OtaContext.h"     // serve our own fw from flash (cache leaves, read payload on demand)
  #include "MerkleTree.h"
  #include <SHA256.h>
  #include <stdlib.h>
  #ifndef OTA_SELF_LEAVES_MAX
  #define OTA_SELF_LEAVES_MAX 65536u   // cap heap for cached leaves (~16k blocks @1 KB = up to ~16 MB image)
  #endif
  #ifndef OTA_SELF_BLOCKS_PER_TICK
  #define OTA_SELF_BLOCKS_PER_TICK 1u    // merkle blocks per loop() tick (~200 ms @ 100 ms/block on nRF52)
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
bool ota_self_firmware(SelfFwInfo& out) {
  const uint8_t* region = (const uint8_t*)(uintptr_t)MOTA_NRF52_APP_BASE;
  uint32_t region_len = MOTA_NRF52_STAGE_CEILING - MOTA_NRF52_APP_BASE;
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
  if ((uint64_t)MOTA_NRF52_APP_BASE + off + len > MOTA_NRF52_STAGE_CEILING) return false;
  memcpy(buf, (const uint8_t*)(uintptr_t)(MOTA_NRF52_APP_BASE + off), len);
  return true;
}
#else
bool ota_self_read(uint32_t, uint8_t*, uint32_t) { return false; }
#endif

#if OTA_SELF_SERVE && (defined(ESP32_PLATFORM) || defined(NRF52_PLATFORM))
struct ServeSelfBuild {
  bool active = false;
  uint32_t image_size = 0;
  uint32_t bc = 0;
  uint32_t next_block = 0;
  uint32_t fw_version = 0;
  SelfFwInfo fi;
  SHA256 sha;
  MerkleRootAcc root_acc;
};
static ServeSelfBuild s_build;

static void serve_self_build_abort(OtaContext& c) {
  free(c.serve_self_leaves);
  free(c.serve_self_proof);
  c.serve_self_leaves = c.serve_self_proof = nullptr;
  s_build = ServeSelfBuild();
}

static bool self_read_cb(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
  (void)ctx; return ota_self_read(off, buf, len);
}

static uint32_t parse_fw_version(const char* s) {
  if (!s) return 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') continue;
    const char* p = s; uint32_t a = 0, b = 0, d = 0; int dots = 0;
    uint32_t* cur = &a;
    for (; *p; p++) {
      if (*p >= '0' && *p <= '9') { *cur = *cur * 10 + (uint32_t)(*p - '0'); }
      else if (*p == '.' && dots < 2) { dots++; cur = (dots == 1) ? &b : &d; }
      else break;
    }
    if (dots >= 1) return FwVersion{ (uint8_t)a, (uint8_t)b, (uint8_t)d, 0 }.pack();
    s = p - 1;
  }
  return 0;
}

static bool serve_self_finish(OtaContext& c, uint32_t fw_version) {
  const uint32_t image_size = s_build.image_size, bc = s_build.bc;
  const SelfFwInfo& fi = s_build.fi;

  OTA_DBG_MS("ota_serve_self: merkle done, manifest");
  uint8_t image_hash[32];
  s_build.sha.finalize(image_hash, 32);
  uint8_t root[4];
  merkle_root_acc_finish(root, s_build.root_acc, bc);

  uint32_t out_target = fi.target_id ? fi.target_id : c.manager.target();
  uint32_t out_ver    = fi.fw_version ? fi.fw_version : fw_version;
  const char* out_hw  = fi.hw_id[0] ? fi.hw_id : c.hw_id;

  uint8_t* m = c.serve_self_manifest;
  memset(m, 0, MOTA_MFL);
  m[0] = MOTA_FORMAT_VER; m[1] = MFLAG_FULL; m[2] = HASH_ALGO_SHA256;
  wr_u32le(m + 3, out_target); wr_u32le(m + 7, out_ver);
  wr_u32le(m + 11, image_size); wr_u32le(m + 15, image_size);
  m[19] = 10;
  memcpy(m + 20, root, 4);
  memcpy(m + 24, image_hash, 32);
  m[56] = CODEC_FULL;
  memcpy(m + 57, out_hw, strlen(out_hw) < 32 ? strlen(out_hw) : 32);
  memcpy(m + MOTA_OFF_APPROVAL, APPROVAL_NOT, 4);

  OTA_DBG_MS("ota_serve_self: manager.serve_self");
  bool ok = c.manager.serve_self(m, MOTA_MFL, c.serve_self_leaves, bc,
                                 c.serve_self_proof, (size_t)bc * 4, self_read_cb, nullptr);
  OTA_DBG_MS("ota_serve_self: serve_self %s", ok ? "ok" : "fail");
  c.serving = ok;
  s_build.active = false;
  return ok;
}

bool ota_serve_self_building() {
  return s_build.active;
}

bool ota_serve_self_begin(OtaContext& c, uint32_t fw_version) {
  if (c.serving) return true;
  if (s_build.active) return true;

  OTA_DBG_MS("ota_serve_self: begin");
#ifdef FIRMWARE_VERSION
  if (fw_version == 0) fw_version = parse_fw_version(FIRMWARE_VERSION);
#endif
  SelfFwInfo fi;
  if (!ota_self_firmware(fi) || !fi.valid) {
    OTA_DBG_MS("ota_serve_self: EndF lookup failed");
    return false;
  }

  const uint32_t image_size = fi.image_len, BS = OTA_DEFAULT_BLOCK_SIZE;
  const uint32_t bc = (image_size + BS - 1) / BS;
  OTA_DBG_MS("ota_serve_self: image=%lu blocks=%lu", (unsigned long)image_size, (unsigned long)bc);
  if ((uint64_t)bc * 4 > OTA_SELF_LEAVES_MAX) {
    OTA_DBG_MS("ota_serve_self: block count exceeds OTA_SELF_LEAVES_MAX");
    return false;
  }

  free(c.serve_self_leaves);
  free(c.serve_self_proof);
  c.serve_self_leaves = (uint8_t*)malloc((size_t)bc * 4);
  c.serve_self_proof  = (uint8_t*)malloc((size_t)bc * 4);
  if (!c.serve_self_leaves || !c.serve_self_proof) {
    OTA_DBG_MS("ota_serve_self: malloc failed bc=%lu", (unsigned long)bc);
    serve_self_build_abort(c);
    return false;
  }

  s_build = ServeSelfBuild();
  s_build.active = true;
  s_build.image_size = image_size;
  s_build.bc = bc;
  s_build.fw_version = fw_version;
  s_build.fi = fi;
  merkle_root_acc_init(s_build.root_acc);
  OTA_DBG_MS("ota_serve_self: malloc ok, merkle loop");
  return true;
}

bool ota_serve_self_tick(OtaContext& c) {
  if (c.serving) return true;
  if (!s_build.active) return false;

  const uint32_t BS = OTA_DEFAULT_BLOCK_SIZE;
  const uint32_t image_size = s_build.image_size;
  const uint32_t bc = s_build.bc;
  uint8_t blk[BS];
  uint32_t n = OTA_SELF_BLOCKS_PER_TICK;

  while (n-- && s_build.next_block < bc) {
    uint32_t i = s_build.next_block;
    uint32_t off = i * BS;
    uint32_t blen = (off + BS <= image_size) ? BS : (image_size - off);
    if (!ota_self_read(off, blk, blen)) {
      OTA_DBG_MS("ota_serve_self: read failed block=%lu off=%lu", (unsigned long)i, (unsigned long)off);
      serve_self_build_abort(c);
      return true;
    }
    merkle_leaf(c.serve_self_leaves + (size_t)i * 4, blk, blen);
    merkle_root_acc_push(s_build.root_acc, c.serve_self_leaves + (size_t)i * 4);
    s_build.sha.update(blk, blen);
    s_build.next_block++;
    if ((i & 63) == 0 && i != 0) {
      OTA_DBG_MS("ota_serve_self: merkle %lu/%lu", (unsigned long)i, (unsigned long)bc);
    }
  }

  if (s_build.next_block < bc) return false;

  serve_self_finish(c, s_build.fw_version);
  return true;
}

bool ota_serve_self(OtaContext& c, uint32_t fw_version) {
  if (c.serving) return true;
  if (!ota_serve_self_begin(c, fw_version)) return false;
  while (!ota_serve_self_tick(c)) {}
  return c.serving;
}
#else
bool ota_serve_self_building() { return false; }
bool ota_serve_self_begin(OtaContext&, uint32_t) { return false; }
bool ota_serve_self_tick(OtaContext&) { return true; }
bool ota_serve_self(OtaContext&, uint32_t) { return false; }
#endif

} // namespace ota
} // namespace mesh
