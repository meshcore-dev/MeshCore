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

// Compute (once) + cache our running firmware's manifest + merkle leaves in `c`, then serve it from
// flash as a full `.mota` (payload read on demand per block; only metadata held in RAM). Returns false
// if no EndF / image too big / OOM. Device platforms only.
struct OtaContext;
bool ota_serve_self_begin(OtaContext& c, uint32_t fw_version);  // start chunked build (no-op if serving)
bool ota_serve_self_tick(OtaContext& c);   // advance build; true when done (check c.serving)
bool ota_serve_self_building();            // merkle build in progress
bool ota_serve_self(OtaContext& c, uint32_t fw_version);        // blocking (CLI); target = own fw

} // namespace ota
} // namespace mesh
