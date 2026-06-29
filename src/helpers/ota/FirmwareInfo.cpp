#include "FirmwareInfo.h"
#include "Multihash.h"
#include "OtaByteIO.h"
#include <string.h>

namespace mesh {
namespace ota {

bool find_self_firmware(const uint8_t* region, uint32_t region_len,
                        SelfFwInfo& out, bool verify_body) {
  out = SelfFwInfo();
  if (!region || region_len < ENDF_LEN) return false;

  for (uint32_t off = 0; off + ENDF_LEN <= region_len; off++) {
    if (region[off] != ENDF_MAGIC[0]) continue;               // cheap pre-filter ('E')
    if (memcmp(region + off, ENDF_MAGIC, 4) != 0) continue;
    uint32_t body_len = rd_u32le(region + off + 4);
    if (body_len != off) continue;                            // trailer must sit right after the body

    if (verify_body) {
      uint8_t h[8];
      mh8(h, region, body_len);
      if (memcmp(h, region + off + 8, 8) != 0) continue;       // coincidental marker — keep scanning
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
    return true;
  }
  return false;
}

} // namespace ota
} // namespace mesh
