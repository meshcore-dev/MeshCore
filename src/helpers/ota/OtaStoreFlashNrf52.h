#pragma once

#if defined(NRF52_PLATFORM) && defined(OTA_FLASH_STORE)

#include "OtaStore.h"
#include "OtaFlashLayout_nrf52.h"

// Persistent flash-backed OtaStore for nRF52 (RAK4631). Stages the received `.mota` in the free flash
// below the primary LittleFS (FS_START), bottom-aligned so its trailer ends at FS_START and the
// bootloader can scan for it. Survives reboot — the whole point — so the bootloader can apply the
// staged delta on the next boot.
//
// RAM is bounded to O(one flash page), NEVER O(mota): a 100 KB+ delta must not live in RAM.
//   - On nRF52 the flash *erase* unit is one 4 KB page and the only SoftDevice-safe writer
//     (Adafruit `flash_nrf5x`) erases the whole page on every flush (~85 ms, CPU stalled → LoRa RX
//     starved). Writing to flash per received packet therefore drops in-flight DATA and the transfer
//     stalls. The fix: coalesce to the *page*, the hardware-natural unit, and write each page once.
//   - `_meta_page` pins flash page 0 (header + manifest + the merkle-leaf progress markers, which are
//     written one-per-block all transfer long). Keeping it in RAM means streaming the payload never
//     re-erases the leaves' page. Flushed once at finalize(). Requires metadata <= one page
//     (set_meta_size enforces it; true for <= ~979 blocks, i.e. any realistic MeshCore image).
//   - `_pay_page` is a single sliding buffer for one payload page (index >= 1). It advances
//     monotonically with the (mostly in-order) block stream and flushes the page it leaves behind.
//     Rare out-of-order writes to an already-flushed page go straight to flash as a safe read-modify-
//     write (flash_nrf5x erases before programming, so re-touching a page never violates the
//     writes-per-word limit — it just costs one extra erase).
//   - The 5-byte trailer is buffered and written at finalize().
// Net: flash is touched ~once per 4 KB page (≈ 1 per 4 blocks at 1 KB), off the per-packet path; page
// 0 and the last page are written at finalize() with the radio idle. For a small delta (whole .mota
// in page 0) there is ZERO flash I/O during the transfer.

namespace mesh {
namespace ota {

class OtaStoreFlashNrf52 : public OtaStore {
  static const uint32_t PG = MOTA_NRF52_FLASH_PAGE;   // 4096

  uint32_t _write_start = 0;        // flash address of container offset 0 (page-aligned)
  uint32_t _total = 0;              // staged container size (0 = none)
  bool     _flushed = false;        // finalize() committed everything to flash

  uint8_t  _meta_page[PG];          // pinned flash page 0 (header + manifest + leaves + 1st payload)
  uint8_t  _pay_page[PG];           // sliding buffer for one payload page (index _pay_idx)
  uint32_t _pay_idx = 0;            // page index currently held in _pay_page (0 = none open; pages >= 1)
  uint8_t  _trailer[5];             // last 5 container bytes (kept in RAM, written at finalize)

  // Bytes from `pos` that stay in one store region (a single flash page, or the trailer tail).
  uint32_t run(uint32_t pos, uint32_t remain) const;
  // RAM home of byte `pos`: read_slot always resolves (flushed pages → memory-mapped flash); write_slot
  // opens/advances the sliding payload page and returns nullptr if `pos` is in an already-flushed page.
  const uint8_t* read_slot(uint32_t pos) const;
  uint8_t* write_slot(uint32_t pos);
  void flush_pay();                 // commit _pay_page to flash (erase + program, one page)
  void flush_page(uint32_t page_idx, const uint8_t* buf);   // write a full page to flash

public:
  bool begin(uint32_t total_size) override;
  bool write(uint32_t offset, const uint8_t* data, uint32_t len) override;
  bool read(uint32_t offset, uint8_t* buf, uint32_t len) const override;
  uint32_t capacity() const override { return MOTA_NRF52_FS_START - MOTA_NRF52_APP_BASE; }
  uint32_t staged_size() const override { return _total; }
  void clear() override { _total = 0; _pay_idx = 0; _flushed = false; }
  bool set_meta_size(uint32_t meta_bytes) override { return meta_bytes <= PG; }  // leaves must fit page 0
  void finalize() override;
  void checkpoint() override;   // persist page 0 (leaves) + the open payload page so a reboot can resume
  bool reopen() override;       // re-attach to a container already staged in flash (scan for it)

  // Contiguous view (flash is memory-mapped). VALID ONLY AFTER finalize() — before that, page 0 and the
  // tail are still in RAM. OtaManager/OtaCli/verify use this only once the transfer is COMPLETE.
  const uint8_t* data() const { return (const uint8_t*)(uintptr_t)_write_start; }
  uint32_t write_start() const { return _write_start; }
};

} // namespace ota
} // namespace mesh

#endif
