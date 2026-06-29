// MeshCore `.mota` on-wire constants and fixed layout — a C++-friendly mirror of
// src/helpers/ota/OtaFormat.h. Keep byte-identical with that file and docs/ota_protocol.md.
//
// The format is FIXED-LAYOUT: every manifest field is at a constant offset and always present
// (base_hash/signer_pubkey/signature are zero-filled when not applicable). Only leaves[] varies.
#pragma once
#include <cstdint>
#include <cstddef>

namespace mota {

// container framing
static constexpr uint8_t MOTA_MAGIC[4]   = {'m','O','T','A'};
static constexpr uint8_t MOTA_TRAILER[5] = {'v','k','4','9','6'};
static constexpr uint8_t ENDF_MAGIC[4]   = {'E','n','d','F'};

static constexpr uint8_t  HASH_ALGO_SHA256 = 0x12;
static constexpr uint8_t  FORMAT_VER       = 0x02;

static constexpr uint8_t  MFLAG_FULL   = 0x01;
static constexpr uint8_t  MFLAG_SIGNED = 0x02;

static constexpr uint8_t  CODEC_FULL               = 0;
static constexpr uint8_t  CODEC_DETOOLS_SEQUENTIAL = 1;   // ESP32 A/B
static constexpr uint8_t  CODEC_DETOOLS_INPLACE    = 2;   // nRF52 single-slot

// hash truncations (sha2-256:N = first N bytes of the SHA-256 digest)
static constexpr size_t MH4 = 4, MH8 = 8, MH32 = 32;

static constexpr uint8_t  APPROVAL_NOT[4] = {0xFF,0xFF,0xFF,0xFF};
static constexpr uint8_t  APPROVAL_YES[4] = {'A','P','R','V'};

// EndF trailer (fixed 56 bytes): marker(4) body_len(4) body_hash8(8) fw_version(4) target_id(4) hw_id(32)
static constexpr uint32_t ENDF_LEN     = 56;
static constexpr uint32_t ENDF_OFF_FWVER  = 16;
static constexpr uint32_t ENDF_OFF_TARGET = 20;
static constexpr uint32_t ENDF_OFF_HWID   = 24;

static constexpr uint8_t  HW_ID_LEN = 32;

// manifest fixed offsets (within the manifest, i.e. container offset = 8 + these)
static constexpr uint32_t M_OFF_FORMAT_VER = 0;
static constexpr uint32_t M_OFF_FLAGS      = 1;
static constexpr uint32_t M_OFF_HASH_ALGO  = 2;
static constexpr uint32_t M_OFF_TARGET_ID  = 3;
static constexpr uint32_t M_OFF_FW_VERSION = 7;
static constexpr uint32_t M_OFF_IMAGE_SIZE = 11;
static constexpr uint32_t M_OFF_PAYLOAD_SIZE = 15;
static constexpr uint32_t M_OFF_BLOCK_SIZE_LOG2 = 19;
static constexpr uint32_t M_OFF_MERKLE_ROOT = 20;   // 4
static constexpr uint32_t M_OFF_IMAGE_HASH  = 24;   // 32
static constexpr uint32_t M_OFF_CODEC_ID    = 56;   // 1
static constexpr uint32_t M_OFF_HW_ID       = 57;   // 32
static constexpr uint32_t M_OFF_BASE_HASH   = 89;   // 8  (zero if FULL)
static constexpr uint32_t M_OFF_SIGNER      = 97;   // 32 (zero if unsigned)
static constexpr uint32_t M_OFF_SIGNATURE   = 129;  // 64 (zero if unsigned)
static constexpr uint32_t M_OFF_APPROVAL    = 193;  // 4
static constexpr uint32_t MOTA_MFL          = 197;  // manifest-minus-leaves length (constant)
static constexpr uint32_t MOTA_SIGNED_LEN   = 129;  // signature covers manifest[0, 129)

static constexpr uint32_t DEFAULT_BLOCK_SIZE = 1024;
static constexpr uint8_t  DEFAULT_BLOCK_SIZE_LOG2 = 10;

// nRF52 in-place apply workspace — MUST match src/helpers/ota/OtaFlashLayout_nrf52.h
// (MOTA_NRF52_INPLACE_MEMORY). It is NOT the full [APP_BASE, FS_START) span: the staged .mota itself sits
// just below FS_START, so the bootloader's workspace ends at the staged container (ws_hi = mota_addr), not
// at FS_START. The workspace is [APP_BASE, 0xBE000) = 0x98000, leaving 0xBE000..0xD4000 (~88 KB) for the
// staged delta. A patch built with a larger memory_size overruns the workspace at apply -> DETOOLS_IO_FAILED
// (the apply silently fails and the device just reboots). Reproduced + verified by the bootloader apply
// simulation (Adafruit_nRF52_Bootloader_OTAFIX/test/apply_sim).
static constexpr uint32_t NRF52_INPLACE_MEMORY  = 0x00098000u;   // 608 KB: [APP_BASE, 0xBE000)
static constexpr uint32_t NRF52_INPLACE_SEGMENT = 4096;
// The staged .mota sits in [APP_BASE+memory, FS_START); a larger container would push its start below the
// workspace end and break the apply the same way. Warn (motatool) / fail (bootloader) past this.
static constexpr uint32_t NRF52_FLASH_SPAN       = 0x000D4000u - 0x00026000u;          // 0xAE000
static constexpr uint32_t NRF52_MAX_INPLACE_MOTA = NRF52_FLASH_SPAN - NRF52_INPLACE_MEMORY;  // 0x16000 (~90 KB)
static_assert(NRF52_INPLACE_MEMORY < NRF52_FLASH_SPAN, "in-place workspace must leave room below FS_START for the staged .mota");

// ---- mota-seeder transport protocol (mirror of src/helpers/ota/MotaSeederProto.h) ----
// Request  (client -> server):  'M' 'S'  op(1)  args...               xsum(1 = XOR of op+args)
// Response (server -> client):  'm' 's'  op(1)  status(1)  payload...  xsum(1 = XOR of all prior)
static constexpr uint8_t MS_REQ_MAGIC0 = 'M', MS_REQ_MAGIC1 = 'S';
static constexpr uint8_t MS_RSP_MAGIC0 = 'm', MS_RSP_MAGIC1 = 's';
static constexpr uint8_t MS_OP_COUNT    = 0x01;   // -> count(1)
static constexpr uint8_t MS_OP_DESCRIBE = 0x02;   // idx(1) -> MotaDesc(38)
static constexpr uint8_t MS_OP_READ     = 0x03;   // idx(1) off(4) len(2) -> bytes
static constexpr uint8_t MS_STATUS_OK   = 0x00;
static constexpr uint8_t MS_STATUS_ERR  = 0x01;
static constexpr uint16_t MOTA_DESC_WIRE = 38;
// MotaDesc wire (38 B): mid[4] target_id(4) fw_version(4) codec(1) flags(1) total_size(4)
//                       leaves_off(4) block_count(4) payload_off(4) payload_size(4)  [+2 reserved]

inline uint32_t rd_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void wr_u32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

} // namespace mota
