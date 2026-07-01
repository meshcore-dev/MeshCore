#pragma once

#include <stdint.h>
#include <stddef.h>
#include "SignerAllowlist.h"

// P6 apply (full-image, ESP32 A/B). The new image is delivered into the inactive OTA slot; the device
// then verifies that slot against the signed manifest's image_hash (+ Ed25519/allowlist), and commits
// by setting it as the boot partition and rebooting. Safe + rollback-capable (the bootloader validates
// the image; a bad image rolls back). nRF52 apply is the bootloader-handoff path (separate). Functions
// return false on platforms without an A/B OTA layout.

namespace mesh {
namespace ota {

struct ApplyState {
  bool     manifest_ok = false;
  bool     sig_ok = false;
  bool     trusted = false;     // signer in allowlist
  bool     slot_ok = false;     // inactive slot image hashes to manifest.image_hash
  uint32_t slot_addr = 0, slot_size = 0;
  uint32_t image_size = 0;
  uint8_t  image_hash[32] = {0};
};

bool ota_apply_slot_info(uint32_t* addr, uint32_t* size);                 // the inactive A/B slot
bool ota_apply_set_manifest(const uint8_t* mf, uint32_t len,
                            const SignerAllowlist& allow, ApplyState& st); // parse + verify signature
bool ota_apply_verify_slot(ApplyState& st);                              // hash the slot vs image_hash
bool ota_apply_commit();                                                 // set-boot + reboot (no return)

// Apply an ESP32 A/B `.mota` (CODEC_DETOOLS_SEQUENTIAL delta or CODEC_FULL image) and arm the inactive
// slot; the caller reboots after the confirmation reply. `msg` (>=80 bytes) receives a human-readable
// result. With OTA_FLASH_STORE the container is staged in the inactive slot (no contiguous RAM copy): a
// full payload is already in the slot (just verified), a sequential delta is decoded from the running
// slot over the staged patch. Without OTA_FLASH_STORE (bring-up) the whole container is a RAM buffer.
#if defined(ESP32_PLATFORM) && defined(OTA_FLASH_STORE)
class OtaStoreFlashEsp32;
bool ota_apply_detools_mota(OtaStoreFlashEsp32& store,
                            const SignerAllowlist& allow, ApplyState& st, char* msg);
#else
bool ota_apply_detools_mota(const uint8_t* buf, uint32_t len,
                            const SignerAllowlist& allow, ApplyState& st, char* msg);
#endif

// nRF52 (RAK4631) single-slot apply. The running app can't rewrite itself, so it does NOT decode: it
// runs the gated verification chain (payload hash -> built-for-this-firmware -> signature/trust) and,
// only if all pass, marks the staged manifest APPROVED in flash. It does NOT reboot — so the caller can
// first send the result back to the operator — the actual handoff is ota_reboot_to_apply() below.
// Returns true (msg = "verified...") when approved, false (msg = the first failing gate) otherwise.
bool ota_apply_mota_nrf52(const uint8_t* buf, uint32_t len,
                          const SignerAllowlist& allow, ApplyState& st, char* msg);

// Commit the (already approved/armed) update and reboot into it — does NOT return. Call this only after
// a successful ota_apply_* AND after the confirmation reply has been delivered, so the operator knows
// the apply started (over LoRa the device then goes silent while the bootloader applies). nRF52: set
// the GPREGRET apply magic + reset (the bootloader does the in-place decode + verify). ESP32: reboot
// into the slot already armed by ota_apply_detools_mota.
void ota_reboot_to_apply();

// DIAGNOSTIC (nRF52): the bootloader stashes its last in-place-apply bail/progress code in GPREGRET2 (see
// ota_delta.c). Read it back so `ota status` can show why an apply didn't take. 0 / other platforms = n/a.
uint8_t ota_bootloader_last_rc();

} // namespace ota
} // namespace mesh
