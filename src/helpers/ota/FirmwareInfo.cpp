#include "FirmwareInfo.h"
#include "Multihash.h"
#include "OtaByteIO.h"
#include "OtaDebug.h"
#include <string.h>
#if defined(NRF52_PLATFORM)
#include <SHA256.h>   // software SHA for EndF body verify (CC310 hash rejects flash-backed buffers)
#endif

namespace mesh {
namespace ota {

#if defined(NRF52_PLATFORM)
static SelfFwInfo s_cached_self;
static bool s_cached_self_valid = false;

static void cache_self_fw(const SelfFwInfo& fi) {
  if (!fi.valid) return;
  s_cached_self = fi;
  s_cached_self_valid = true;
}
#endif

#if defined(NRF52_PLATFORM)
// nRF52 app flash is memory-mapped, but CC310 CRYS_HASH expects SRAM — hash body in chunks.
static void mh8_region_chunked(uint8_t out[8], const uint8_t* region, uint32_t len) {
  OTA_DBG_MS("EndF: body hash start len=%lu", (unsigned long)len);
  SHA256 sha;
  const uint32_t CH = 512;
  uint8_t buf[512];
  for (uint32_t pos = 0; pos < len; pos += CH) {
    uint32_t n = len - pos;
    if (n > CH) n = CH;
    memcpy(buf, region + pos, n);
    sha.update(buf, n);
    if ((pos & 0xFFFF) == 0 && pos != 0) {
      OTA_DBG_MS("EndF: body hash %lu/%lu", (unsigned long)pos, (unsigned long)len);
    }
  }
  sha.finalize(out, 8);
  OTA_DBG_MS("EndF: body hash done");
}
#endif

bool find_self_firmware(const uint8_t* region, uint32_t region_len,
                        SelfFwInfo& out, bool verify_body) {
  out = SelfFwInfo();
  if (!region || region_len < ENDF_LEN) return false;

#if defined(NRF52_PLATFORM)
  if (s_cached_self_valid && (!verify_body || s_cached_self.body_len > 0)) {
    out = s_cached_self;
    OTA_DBG_MS("EndF: cache hit image_len=%lu", (unsigned long)out.image_len);
    return true;
  }
#endif

  OTA_DBG_MS("EndF: scan start region_len=%lu verify=%d",
             (unsigned long)region_len, verify_body ? 1 : 0);
  for (uint32_t off = 0; off + ENDF_LEN <= region_len; off++) {
    if ((off & 0x1FFFF) == 0 && off != 0) {
      OTA_DBG_MS("EndF: scan at off=%lu/%lu", (unsigned long)off, (unsigned long)region_len);
    }
    if (region[off] != ENDF_MAGIC[0]) continue;               // cheap pre-filter ('E')
    if (memcmp(region + off, ENDF_MAGIC, 4) != 0) continue;
    uint32_t body_len = rd_u32le(region + off + 4);
    if (body_len != off) continue;                            // trailer must sit right after the body

    OTA_DBG_MS("EndF: candidate off=%lu body_len=%lu", (unsigned long)off, (unsigned long)body_len);
    if (verify_body) {
      uint8_t h[8];
#if defined(NRF52_PLATFORM)
      mh8_region_chunked(h, region, body_len);
#else
      mh8(h, region, body_len);
#endif
      if (memcmp(h, region + off + 8, 8) != 0) {
        OTA_DBG_MS("EndF: candidate hash mismatch, keep scanning");
        continue;       // coincidental marker — keep scanning
      }
    }
    out.valid = true;
    out.endf_offset = off;
    out.body_len = body_len;
    out.image_len = off + ENDF_LEN;
    memcpy(out.body_hash, region + off + 8, 8);
    // Fixed 56-byte trailer: the self-describing identity follows body_hash at constant offsets
    // (fw_version@16, target_id@20, hw_id@24..56). Zero/"" means "unknown".
    out.fw_version = rd_u32le(region + off + 16);
    out.target_id  = rd_u32le(region + off + 20);
    memcpy(out.hw_id, region + off + 24, 32);
    out.hw_id[32] = 0;
    OTA_DBG_MS("EndF: found image_len=%lu target=%08lX ver=%08lX",
               (unsigned long)out.image_len, (unsigned long)out.target_id, (unsigned long)out.fw_version);
#if defined(NRF52_PLATFORM)
    cache_self_fw(out);
#endif
    return true;
  }
  OTA_DBG_MS("EndF: scan done, not found");
  return false;
}

} // namespace ota
} // namespace mesh
