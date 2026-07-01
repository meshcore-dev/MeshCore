#include "OtaStoreFlashNrf52.h"

#if defined(NRF52_PLATFORM) && defined(OTA_FLASH_STORE)

#include "OtaSelf.h"
#include "OtaDebug.h"
#include "OtaByteIO.h"           // align_down / rd_u32le (flash-page geometry + header read)
#include <string.h>
#include "flash/flash_nrf5x.h"   // Adafruit core internal-flash driver (SoftDevice-safe; LittleFS path)

namespace mesh {
namespace ota {

// Write one whole 4 KB page from `buf` to flash (erase + program, ~85 ms). `buf` is PG bytes, 0xFF-padded
// past the container, so the program is clean. The last container page ends exactly at FS_START (both
// FS_START and _write_start are page-aligned and the container ends <= FS_START), so a full-page write
// never reaches into ExtraFS.
void OtaStoreFlashNrf52::flush_page(uint32_t page_idx, const uint8_t* buf) {
  uint32_t addr = _write_start + page_idx * PG;
  if (addr + PG > MOTA_NRF52_FS_START) return;        // defensive: never cross the staging ceiling
  OTA_DBG("OTA flash: write page %u @ %08x\n", (unsigned)page_idx, (unsigned)addr);
  flash_nrf5x_write(addr, buf, PG);
  flash_nrf5x_flush();
}

void OtaStoreFlashNrf52::flush_pay() {
  if (_pay_idx != 0) flush_page(_pay_idx, _pay_page);   // _pay_idx 0 == no payload page open
}

uint32_t OtaStoreFlashNrf52::run(uint32_t pos, uint32_t remain) const {
  uint32_t trailer = _total - 5;                        // container is always >= 13 bytes (begin checks)
  if (pos >= trailer) return remain;                    // tail: caller bounds remain to <= 5 already
  uint32_t end = pos + remain;
  uint32_t page_end = (pos / PG + 1) * PG;
  if (end > page_end) end = page_end;                   // a run stays within one flash page,
  if (end > trailer)  end = trailer;                    // and never crosses into the trailer tail
  return end - pos;
}

const uint8_t* OtaStoreFlashNrf52::read_slot(uint32_t pos) const {
  if (pos >= _total - 5)     return _trailer + (pos - (_total - 5));   // trailer tail (RAM until finalize)
  uint32_t page = pos / PG;
  if (page == 0)             return _meta_page + pos;                  // pinned page 0 (incl. leaves)
  if (page == _pay_idx)      return _pay_page + (pos - page * PG);     // current sliding payload page
  return (const uint8_t*)(uintptr_t)(_write_start + pos);              // already flushed -> memory-mapped
}

uint8_t* OtaStoreFlashNrf52::write_slot(uint32_t pos) {
  if (pos >= _total - 5)     return _trailer + (pos - (_total - 5));
  uint32_t page = pos / PG;
  if (page == 0)             return _meta_page + pos;
  if (page > _pay_idx) { flush_pay(); _pay_idx = page; memset(_pay_page, 0xFF, PG); }  // advance, fresh page
  if (page == _pay_idx)      return _pay_page + (pos - page * PG);
  return nullptr;                                                      // page < _pay_idx: already flushed
}

bool OtaStoreFlashNrf52::begin(uint32_t total_size) {
  clear();
  if (total_size < 13 || total_size > capacity()) return false;   // 13 = header(8) + trailer(5)

  // bottom-align against FS_START so the trailer ends exactly at FS_START (bootloader scans for it)
  uint32_t start = align_down(MOTA_NRF52_FS_START - total_size, PG);

  // never collide with the running application image (its extent comes from its EndF trailer)
  uint32_t app_end = MOTA_NRF52_APP_BASE;
  SelfFwInfo fi;
  if (ota_self_firmware(fi) && fi.valid) app_end = MOTA_NRF52_APP_BASE + fi.image_len;
  if (start < app_end) return false;

  _write_start = start;
  _total = total_size;
  memset(_meta_page, 0xFF, PG);     // assemble page 0 in RAM; 0xFF = erased sentinel (unfilled leaf slots)
  memset(_trailer, 0xFF, sizeof(_trailer));
  _pay_idx = 0;
  _flushed = false;
  OTA_DBG("OTA flash: begin total=%u start=%08x app_end=%08x\n",
          (unsigned)total_size, (unsigned)start, (unsigned)app_end);
  return true;                      // no pre-erase: each page is erased by its own (single) flush
}

bool OtaStoreFlashNrf52::write(uint32_t offset, const uint8_t* d, uint32_t len) {
  if ((uint64_t)offset + len > _total) return false;
  for (uint32_t pos = offset, end = offset + len; pos < end; ) {
    uint32_t n = run(pos, end - pos);
    if (uint8_t* dst = write_slot(pos)) {
      memcpy(dst, d, n);
    } else {
      // out-of-order write to an already-flushed page: read-modify-write straight to flash. Safe -- the
      // driver erases the page before programming, so re-touching it never breaks writes-per-word.
      OTA_DBG("OTA flash: RMW page %u (out-of-order) @ off %u\n", (unsigned)(pos / PG), (unsigned)pos);
      if (flash_nrf5x_write(_write_start + pos, d, n) < 0) return false;
      flash_nrf5x_flush();
    }
    pos += n; d += n;
  }
  return true;
}

bool OtaStoreFlashNrf52::read(uint32_t offset, uint8_t* buf, uint32_t len) const {
  if ((uint64_t)offset + len > _total) return false;
  for (uint32_t pos = offset, end = offset + len; pos < end; ) {
    uint32_t n = run(pos, end - pos);
    memcpy(buf, read_slot(pos), n);
    pos += n; buf += n;
  }
  return true;
}

void OtaStoreFlashNrf52::finalize() {
  if (_flushed || _total == 0) return;
  OTA_DBG("OTA flash: finalize total=%u\n", (unsigned)_total);
  flush_pay();                 // the last (highest) payload page, if one is open
  flush_page(0, _meta_page);   // page 0: header + manifest + leaves + first payload bytes
  flash_nrf5x_write(_write_start + _total - 5, _trailer, 5);   // trailer tail (radio idle at COMPLETE)
  flash_nrf5x_flush();
  _flushed = true;
}

// Persist mid-transfer progress so a reboot can resume. Order matters for consistency: flush the open
// payload page FIRST, then page 0 (the leaf-progress markers) -- so every block whose leaf is now in flash
// also has its payload in flash. Infrequent (every OTA_CHECKPOINT_BLOCKS blocks), so the 2 extra page
// erases don't matter; at LoRa block rates it's roughly once per many minutes.
void OtaStoreFlashNrf52::checkpoint() {
  if (_total == 0 || _flushed) return;
  flush_pay();                 // keep _pay_idx open (it may still receive writes); just re-flush its bytes
  flush_page(0, _meta_page);   // header + manifest + leaves accumulated so far
}

// Re-attach to a container already staged in flash (after a reboot), without erasing. The container is
// bottom-aligned (begin: start = (FS_START - total) & ~(PG-1)) and flash is memory-mapped, so scan page
// starts from just below FS_START down to the app end for MOTA_MAGIC with a self-consistent total; adopt
// the first match (highest address = most recent for the common single-container case). The manager then
// parses the loaded manifest and validates geometry/root, so a stale leftover is rejected there.
bool OtaStoreFlashNrf52::reopen() {
  uint32_t app_end = MOTA_NRF52_APP_BASE;
  SelfFwInfo fi;
  if (ota_self_firmware(fi) && fi.valid) app_end = MOTA_NRF52_APP_BASE + fi.image_len;
  for (uint32_t start = align_down(MOTA_NRF52_FS_START - PG, PG); start >= app_end; start -= PG) {
    const uint8_t* p = (const uint8_t*)(uintptr_t)start;
    if (memcmp(p, MOTA_MAGIC, 4) != 0) continue;
    uint32_t total = rd_u32le(p + 4);
    if (total < 13 || total > capacity()) continue;
    if (align_down(MOTA_NRF52_FS_START - total, PG) != start) continue;   // must match begin()'s placement
    _write_start = start;
    _total = total;
    memcpy(_meta_page, p, PG);                  // load page 0 (header+manifest+leaves) into RAM to continue
    memcpy(_trailer, p + (total - 5), 5);        // recover the trailer tail (flushed at last finalize, if any)
    _pay_idx = 0;
    _flushed = false;
    OTA_DBG("OTA flash: reopen total=%u start=%08x\n", (unsigned)total, (unsigned)start);
    return true;
  }
  return false;
}

} // namespace ota
} // namespace mesh

#endif
