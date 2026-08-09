#pragma once

#if defined(ESP32_PLATFORM) && defined(OTA_FLASH_STORE)

#include "OtaStore.h"
#include "esp_partition.h"

// Persistent flash-backed OtaStore for ESP32 (A/B). Stages the received `.mota` in the INACTIVE OTA
// slot, so a delta/full of any size is bounded to O(one sector) of RAM, never O(mota) -- lifting the old
// RAM store's ~16 KB ceiling (a full 1 MB+ image now fetches over the air). Placement is chosen from the
// parsed manifest (plan_layout), keyed only on full-vs-delta:
//
//   - DELTA (codec sequential/in-place): the whole container is staged bottom-aligned in the slot. At
//     apply the decoder reads the patch from it -- sequential: base from the running slot -> output to
//     the inactive slot from offset 0; in-place: copy running->slot then patch in place. The decoded
//     image fills the slot from offset 0, so plan_layout refuses unless `image_size + container` fits
//     (the output must never reach the bottom-staged container; the fit makes them disjoint).
//
//   - FULL (codec full): the payload IS the final image (no decode), so it streams straight to slot
//     offset 0 while the small header+manifest+merkle-leaves and the trailer persist at the bottom of the
//     slot (so the whole container survives a reboot -- for fetch-resume and for re-serving to other
//     nodes). One default for every A/B full payload; plan_layout refuses unless `image_size + meta`
//     fits, and if that doesn't fit nothing would.
//
// RX-safety: an ESP32 flash erase+write disables the XIP instruction cache and stalls code running from
// flash (the dispatcher loop), exactly like the nRF52 page erase that starved the LoRa RX. So writes are
// COALESCED to the 4 KB sector and each sector is programmed once. The meta region (header+manifest+
// leaves -- written all transfer long as blocks/leaves arrive, often out of order) is PINNED in RAM and
// flushed at finalize() with the radio idle; the bulk payload streams through ONE sliding sector buffer,
// flushing the sector it leaves behind (~1 flush per 4 KB, off the per-packet path). OTA is also the
// lowest-priority TX, so a brief stall yields to real traffic. A small delta whose container fits the
// pinned meta region does ZERO flash I/O until COMPLETE.

namespace mesh {
namespace ota {

#ifndef OTA_ESP32_META_CAP
#define OTA_ESP32_META_CAP 65536   // max heap for header+manifest+merkle leaves (4 B/block) -> ~16k blocks
#endif                             // (a 1.27 MB full image at 128 B LoRa blocks ~= 40 KB of leaves)

class OtaStoreFlashEsp32 : public OtaStore {
  static const uint32_t SEC = 4096;          // ESP32 NOR flash erase unit

  const esp_partition_t* _part = nullptr;    // inactive OTA slot (acquired in plan_layout/begin)
  uint32_t _psize = 0;                       // slot size

  // container geometry (from plan_layout / handleManifest)
  uint32_t _total = 0;                       // container size (0 = none staged)
  uint32_t _meta_bytes = 0;                  // header+manifest+leaves (== payload offset in the container)
  uint32_t _pay_size = 0;                    // payload bytes
  uint32_t _image_size = 0;                  // reconstructed image (delta) / == _pay_size (full)
  bool     _full = false;

  // logical->partition placement (see header doc). For delta everything is contiguous at _write_start;
  // for full the payload lands at slot 0 and meta+trailer at the bottom.
  uint32_t _write_start = 0;                 // delta: container offset 0 in the slot (sector-aligned)
  uint32_t _meta_span = 0;                   // container bytes held in the RAM meta buffer (whole sectors)
  uint32_t _meta_part = 0;                   // slot offset the meta buffer flushes to
  uint32_t _pay_log0 = 0;                    // first container offset that streams to the payload region
  uint32_t _pay_part0 = 0;                   // slot offset of that first payload byte
  uint32_t _trailer_part = 0;                // slot offset of the 5-byte trailer

  // RX-safe staging buffers
  uint8_t* _meta = nullptr;                  // heap, sized per fetch: header+manifest+leaves(+full trailer)
  uint8_t  _pay[SEC];                        // one sliding payload sector (slot-sector aligned)
  uint32_t _pay_sec = 0;                     // slot sector index currently in _pay (0 = none open)
  uint8_t  _trailer[5];
  uint32_t _meta_flush = 0;                  // whole-sector byte count to program for the meta buffer
  uint32_t _pay_max_sec = 0;                 // highest payload slot-sector opened (out-of-order detection)
  bool     _pay_open = false;                // a sliding sector is currently buffered in _pay
  bool     _flushed = false;
  bool     _io_ok = true;                    // cleared if any flash erase/write/read fails

  bool acquire();                            // resolve the inactive slot (idempotent)
  bool layout();                             // compute placement from _full/_image_size/_meta_bytes/_pay_size
  uint32_t pay_part(uint32_t L) const { return _pay_part0 + (L - _pay_log0); }   // payload slot offset
  bool in_trailer(uint32_t L) const { return L >= _total - 5; }
  uint32_t run(uint32_t pos, uint32_t remain) const;   // bytes from `pos` that stay in one region+sector
  uint8_t* meta_slot(uint32_t L);            // RAM home of a meta/trailer byte (nullptr if it's payload)
  const uint8_t* meta_slot_c(uint32_t L) const;
  void open_pay(uint32_t sec);               // make `sec` the buffered sliding sector (RMW if revisited)
  void flush_pay();                          // erase+write the open sliding sector
  void flush_sector(uint32_t slot_off, const uint8_t* buf, uint32_t n);   // erase+program sector(s)

public:
  ~OtaStoreFlashEsp32() override;
  bool plan_layout(bool is_full, uint32_t image_size, uint32_t payload_off, uint32_t payload_size) override;
  bool begin(uint32_t total_size) override;
  bool write(uint32_t offset, const uint8_t* data, uint32_t len) override;
  bool read(uint32_t offset, uint8_t* buf, uint32_t len) const override;
  uint32_t capacity() const override { return _psize; }   // loose bound; plan_layout does the real check
  uint32_t staged_size() const override { return _total; }
  void clear() override;
  bool set_meta_size(uint32_t meta_bytes) override { return meta_bytes < OTA_ESP32_META_CAP; }
  void finalize() override;
  void checkpoint() override;   // persist meta(leaves) + open payload sector so a reboot can resume
  bool reopen() override;       // re-attach to a container already staged in the slot (scan + rebuild geometry)

  // No contiguous RAM/mmap view: the staged container lives in the slot (split for FULL). The ESP32
  // apply path reads what it needs via read()/esp_partition_read instead of a data() pointer, so this
  // returns nullptr (kept for interface parity with the nRF52 store, whose flash is memory-mapped).
  const uint8_t* data() const { return nullptr; }

  // Apply-path accessors: where the staged container physically lives in the inactive slot.
  const esp_partition_t* partition() const { return _part; }
  uint32_t write_start() const { return _write_start; }   // delta: container offset 0 in the slot
  bool is_full() const { return _full; }
  uint32_t image_size() const { return _image_size; }
  uint32_t meta_bytes() const { return _meta_bytes; }
  uint32_t payload_slot_off(uint32_t k) const { return _pay_part0 + k; }   // slot off of payload byte k
};

} // namespace ota
} // namespace mesh

#endif
