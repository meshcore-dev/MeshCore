#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "OtaFormat.h"   // MOTA_MAGIC (resume: detect a persisted partial container)

// Staging backend for an in-transit `.mota` (docs/ota_protocol.md §7). Blocks may arrive out of order
// and progress must survive reboots, so the store is random-access. The transfer/verify logic is
// written against this interface; concrete impls are per-platform (RAM for tests/bring-up; persistent
// flash — ESP32 OTA slot / nRF52 raw region — for production, dropped in behind the same interface).

namespace mesh {
namespace ota {

class OtaStore {
public:
  virtual ~OtaStore() {}
  // Prepare staging for a container of `total_size` bytes (erases/clears). false if it won't fit.
  virtual bool begin(uint32_t total_size) = 0;
  virtual bool write(uint32_t offset, const uint8_t* data, uint32_t len) = 0;
  virtual bool read(uint32_t offset, uint8_t* buf, uint32_t len) const = 0;
  virtual uint32_t capacity() const = 0;
  virtual uint32_t staged_size() const = 0;   // total_size from begin(), 0 if none
  virtual void clear() = 0;

  // Optional: declare the size of the leading metadata (header + manifest + merkle leaves, i.e.
  // everything before the payload). A flash-backed store keeps that region — which is updated
  // throughout the transfer (a leaf is committed per block) — pinned in one RAM page, so it can
  // flush the bulk payload page-by-page without re-erasing the leaves' page on every block.
  // Returns false if the metadata won't fit the store's pinned region (transfer is then refused).
  virtual bool set_meta_size(uint32_t meta_bytes) { (void)meta_bytes; return true; }

  // Optional: commit any RAM-buffered data to persistent storage. Called once when the transfer
  // reaches COMPLETE (radio idle), so a flash store does its page writes off the RX critical path.
  // After this returns, a flash store's data() view is coherent. No-op for purely in-RAM stores.
  virtual void finalize() {}

  // Optional: persist in-progress state (the metadata/leaf-progress page + any open payload buffer) so a
  // reboot mid-transfer can resume. Called by OtaManager every OTA_CHECKPOINT_BLOCKS committed blocks.
  // A flash store flushes its pinned meta page + the open payload page (consistency: payload-before-leaves)
  // so every block whose leaf is persisted also has its payload in flash. No-op for RAM stores. Infrequent
  // (at LoRa block rates, ~once per many minutes) so the extra erases don't matter.
  virtual void checkpoint() {}

  // Optional: re-attach to a container ALREADY persisted in the backing store (after a reboot), WITHOUT
  // erasing. Returns true if a syntactically valid container (MOTA_MAGIC header + plausible total) is
  // present and the store is now set up to read/continue-writing it; false if none (caller starts fresh).
  // OtaManager then reads + parses the stored manifest to recompute geometry and resume the fetch.
  virtual bool reopen() { return false; }

  // Optional: declare the container's logical layout once the manifest is parsed, BEFORE begin(), so a
  // store backed by a single spare A/B partition (ESP32) can choose placement and reject an unfittable
  // fetch up front. A FULL image's payload IS the final firmware (no decode), so it can stream straight
  // to the inactive slot's offset 0 while the small meta/leaves/trailer persist elsewhere; a delta's
  // whole container is staged together (the decoder reads the patch from it at apply). image_size is the
  // reconstructed image; [payload_off, payload_off+payload_size) is the payload region in the container.
  // Return false if it cannot fit the backing store (the transfer is then refused before any block).
  virtual bool plan_layout(bool is_full, uint32_t image_size, uint32_t payload_off, uint32_t payload_size) {
    (void)is_full; (void)image_size; (void)payload_off; (void)payload_size; return true;
  }
};

// Fixed-capacity RAM store — for native tests and device bring-up of the transfer/verify path.
// (Does NOT survive reboot; a persistent flash store replaces it for production — see D1.)
template <uint32_t CAP>
class OtaStoreRam : public OtaStore {
  uint8_t _buf[CAP] = {};   // zero-init so a never-written store's reopen() finds no MOTA_MAGIC
  uint32_t _total = 0;
public:
  bool begin(uint32_t total_size) override {
    if (total_size > CAP) return false;
    _total = total_size;
    memset(_buf, 0xFF, total_size);   // mimic erased flash (so unfilled leaf slots read as 'missing')
    return true;
  }
  bool write(uint32_t off, const uint8_t* d, uint32_t len) override {
    if ((uint64_t)off + len > _total) return false;
    memcpy(_buf + off, d, len);
    return true;
  }
  bool read(uint32_t off, uint8_t* b, uint32_t len) const override {
    if ((uint64_t)off + len > _total) return false;
    memcpy(b, _buf + off, len);
    return true;
  }
  uint32_t capacity() const override { return CAP; }
  uint32_t staged_size() const override { return _total; }
  void clear() override { _total = 0; }
  // RAM doesn't survive a real reboot, but the buffer persists within a process — enough to exercise the
  // manager's resume path in native tests. Recover `total` from the stored header so read() bounds work.
  bool reopen() override {
    if (memcmp(_buf, MOTA_MAGIC, 4) != 0) return false;
    uint32_t t = (uint32_t)_buf[4] | ((uint32_t)_buf[5] << 8) | ((uint32_t)_buf[6] << 16) | ((uint32_t)_buf[7] << 24);
    if (t < 13 || t > CAP) return false;
    _total = t;
    return true;
  }
  const uint8_t* data() const { return _buf; }   // contiguous view (RAM store only)
};

} // namespace ota
} // namespace mesh
