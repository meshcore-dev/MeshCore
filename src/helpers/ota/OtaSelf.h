#pragma once

#include "FirmwareInfo.h"

// Device-side accessor for the running firmware's own image (to read its EndF trailer).
// Per-platform: ESP32 memory-maps the running app partition; other platforms TBD (nRF52 uses the
// bootloader-apply path, so its app-region wiring lands with that work). Not compiled on the native
// host — the portable scan logic in FirmwareInfo.{h,cpp} is what gets unit-tested there.

namespace mesh {
namespace ota {

// Locate this firmware's EndF trailer in its own flash image. Returns false if unsupported on this
// platform or no valid EndF is present (e.g. firmware built without the EndF build hook).
bool ota_self_firmware(SelfFwInfo& out);

// Read `len` bytes of the running firmware image at offset `off` (ESP32: running partition via
// esp_partition_read; nRF52: memory-mapped app region). false on unsupported platforms.
bool ota_self_read(uint32_t off, uint8_t* buf, uint32_t len);

// Flash-backed full-image self-serve (gated by OTA_SELF_SERVE; disabled v0.2.0, remove v0.3.0).
struct OtaContext;
bool ota_serve_self_begin(OtaContext& c, uint32_t fw_version);
bool ota_serve_self_tick(OtaContext& c);
bool ota_serve_self_building();
bool ota_serve_self(OtaContext& c, uint32_t fw_version);

} // namespace ota
} // namespace mesh
