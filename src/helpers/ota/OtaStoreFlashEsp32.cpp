#include "OtaStoreFlashEsp32.h"

#if defined(ESP32_PLATFORM) && defined(OTA_FLASH_STORE)

#include "OtaDebug.h"
#include "OtaByteIO.h"          // align_up / align_down (flash-sector geometry)
#include "MotaContainer.h"      // mota_parse_manifest (reopen: rebuild geometry from the staged manifest)
#include <string.h>
#include <stdlib.h>             // malloc/free (the meta buffer is sized per fetch)
#include "esp_ota_ops.h"        // esp_ota_get_next_update_partition (the inactive A/B slot)

namespace mesh {
namespace ota {

OtaStoreFlashEsp32::~OtaStoreFlashEsp32() { free(_meta); }

bool OtaStoreFlashEsp32::acquire() {
  if (!_part) {
    _part = esp_ota_get_next_update_partition(nullptr);
    _psize = _part ? _part->size : 0;
  }
  return _part != nullptr;
}

// Compute the slot placement from _full/_image_size/_meta_bytes/_pay_size (already set). Returns false if
// it won't fit. Shared by plan_layout (fresh fetch) and reopen (resume) so both derive identical geometry.
bool OtaStoreFlashEsp32::layout() {
  uint32_t total = _meta_bytes + _pay_size + 5;
  if (_full) {
    // payload streams to slot offset 0 (it IS the image); header+manifest+leaves+trailer persist at the
    // bottom so the container survives a reboot (resume / re-serve).
    _meta_span    = _meta_bytes;                       // routing boundary: [0,meta) -> RAM meta buffer
    _meta_flush   = align_up(_meta_bytes + 5, SEC);    // meta + 5-byte trailer, whole sectors
    if (_meta_flush > OTA_ESP32_META_CAP) return false;
    uint32_t bottom = align_down(_psize - _meta_flush, SEC);
    _meta_part  = bottom;
    _pay_log0   = _meta_bytes; _pay_part0 = 0;
    _write_start = 0;
    if (_image_size > bottom) return false;            // image would overrun the bottom meta region
  } else {
    // whole container staged bottom-aligned; the decoded image fills the slot from offset 0.
    _meta_span  = align_up(_meta_bytes, SEC);          // pin whole sectors covering meta (+ spillover payload)
    _meta_flush = _meta_span;
    if (_meta_flush > OTA_ESP32_META_CAP) return false;
    if (total > _psize) return false;
    _write_start = align_down(_psize - total, SEC);
    _meta_part   = _write_start;
    _pay_log0    = _meta_span; _pay_part0 = _write_start + _meta_span;
    if (_image_size > _write_start) return false;      // decoded output would overlap the staged container
  }
  _total = total;
  return true;
}

// Choose placement from the parsed manifest and refuse anything that won't fit, BEFORE any block is
// staged. (See the header for the delta-vs-full layout rationale.)
bool OtaStoreFlashEsp32::plan_layout(bool is_full, uint32_t image_size, uint32_t payload_off, uint32_t payload_size) {
  if (!acquire()) return false;
  _full = is_full; _image_size = image_size; _meta_bytes = payload_off; _pay_size = payload_size;
  bool ok = layout();
  OTA_DBG("OTA esp32: plan %s total=%u image=%u meta=%u write_start=%u meta_part=%u ok=%d\n",
          is_full ? "FULL" : "DELTA", (unsigned)_total, (unsigned)image_size, (unsigned)_meta_bytes,
          (unsigned)_write_start, (unsigned)_meta_part, (int)ok);
  return ok;
}

bool OtaStoreFlashEsp32::begin(uint32_t total_size) {
  if (!_part || _total == 0 || total_size != _total) return false;   // plan_layout must have run + agreed
  free(_meta);
  _meta = (uint8_t*)malloc(_meta_flush);                             // header+manifest+leaves(+full trailer)
  if (!_meta) { _total = 0; return false; }
  memset(_meta, 0xFF, _meta_flush);
  memset(_trailer, 0xFF, sizeof(_trailer));
  _pay_open = false; _pay_sec = 0; _pay_max_sec = 0; _flushed = false;
  _io_ok = true;
  return true;
}

void OtaStoreFlashEsp32::clear() {
  free(_meta); _meta = nullptr;
  _total = 0; _pay_open = false; _flushed = false;   // _part kept (re-acquire is fine)
}

uint8_t* OtaStoreFlashEsp32::meta_slot(uint32_t L) {
  if (in_trailer(L)) return _full ? (_meta + _meta_bytes + (L - (_total - 5)))
                                  : (_trailer + (L - (_total - 5)));
  if (L < _meta_span) return _meta + L;
  return nullptr;                                    // payload -> sliding sector / flash
}
const uint8_t* OtaStoreFlashEsp32::meta_slot_c(uint32_t L) const {
  if (in_trailer(L)) return _full ? (_meta + _meta_bytes + (L - (_total - 5)))
                                  : (_trailer + (L - (_total - 5)));
  if (L < _meta_span) return _meta + L;
  return nullptr;
}

// Bytes from `pos` that stay in one region, and (for payload) one flash sector.
uint32_t OtaStoreFlashEsp32::run(uint32_t pos, uint32_t remain) const {
  if (pos >= _total - 5) return remain;                          // trailer (<=5, one buffer)
  if (pos < _meta_span) { uint32_t c = _meta_span - pos; return remain < c ? remain : c; }
  uint32_t poff = pay_part(pos);
  uint32_t to_sec = SEC - (poff % SEC);
  uint32_t to_end = (_total - 5) - pos;                          // don't cross into the trailer
  uint32_t c = remain;
  if (to_sec < c) c = to_sec;
  if (to_end < c) c = to_end;
  return c;
}

void OtaStoreFlashEsp32::flush_sector(uint32_t slot_off, const uint8_t* buf, uint32_t n) {
  if (!_io_ok) return;
  if (esp_partition_erase_range(_part, slot_off, n) != ESP_OK) { _io_ok = false; return; }
  if (esp_partition_write(_part, slot_off, buf, n) != ESP_OK)   { _io_ok = false; return; }
  OTA_DBG("OTA esp32: flushed %u B @ slot+%u\n", (unsigned)n, (unsigned)slot_off);
}

void OtaStoreFlashEsp32::flush_pay() {
  if (_pay_open) { flush_sector((uint32_t)_pay_sec * SEC, _pay, SEC); _pay_open = false; }
}

void OtaStoreFlashEsp32::open_pay(uint32_t sec) {
  if (_pay_open) flush_pay();
  if (sec < _pay_max_sec) {
    // revisiting an already-flushed sector (out-of-order block) -> read it back so the gaps we don't
    // touch are preserved (they were programmed as 0xFF or earlier block data); we erase+reprogram on flush.
    if (esp_partition_read(_part, (size_t)sec * SEC, _pay, SEC) != ESP_OK) _io_ok = false;
  } else {
    memset(_pay, 0xFF, SEC);                          // fresh sector
    _pay_max_sec = sec;
  }
  _pay_sec = sec; _pay_open = true;
}

bool OtaStoreFlashEsp32::write(uint32_t offset, const uint8_t* d, uint32_t len) {
  if ((uint64_t)offset + len > _total || !_io_ok) return false;
  for (uint32_t pos = offset, end = offset + len; pos < end; ) {
    uint32_t n = run(pos, end - pos);
    if (uint8_t* dst = meta_slot(pos)) {              // meta / leaves / trailer -> pinned RAM
      memcpy(dst, d, n);
    } else {                                          // payload -> sliding sector
      uint32_t poff = pay_part(pos);
      uint32_t sec  = poff / SEC;
      if (!_pay_open || sec != _pay_sec) open_pay(sec);
      memcpy(_pay + (poff % SEC), d, n);
    }
    pos += n; d += n;
    if (!_io_ok) return false;
  }
  return true;
}

bool OtaStoreFlashEsp32::read(uint32_t offset, uint8_t* buf, uint32_t len) const {
  if ((uint64_t)offset + len > _total) return false;
  for (uint32_t pos = offset, end = offset + len; pos < end; ) {
    uint32_t n = run(pos, end - pos);
    if (const uint8_t* src = meta_slot_c(pos)) {
      memcpy(buf, src, n);
    } else {
      uint32_t poff = pay_part(pos);
      if (_pay_open && poff / SEC == _pay_sec) memcpy(buf, _pay + (poff % SEC), n);   // still in RAM
      else if (esp_partition_read(_part, poff, buf, n) != ESP_OK) return false;       // flushed -> flash
    }
    pos += n; buf += n;
  }
  return true;
}

void OtaStoreFlashEsp32::finalize() {
  if (_flushed || _total == 0) return;
  if (!_full) {
    // delta: drop the 5-byte trailer into the sliding payload sector(s) so it flushes with them (the
    // trailer sits right after the payload; this also covers the rare case it spills into a fresh sector).
    uint32_t tpoff = _write_start + (_total - 5);
    for (uint32_t off = 0; off < 5; ) {
      uint32_t sec = (tpoff + off) / SEC;
      if (!_pay_open || sec != _pay_sec) open_pay(sec);
      uint32_t in = SEC - ((tpoff + off) % SEC); if (in > 5 - off) in = 5 - off;
      memcpy(_pay + ((tpoff + off) % SEC), _trailer + off, in);
      off += in;
    }
  }
  flush_pay();                                        // last payload sector(s) (+ delta trailer)
  flush_sector(_meta_part, _meta, _meta_flush);       // meta (+ trailer for full)
  _flushed = true;
  OTA_DBG("OTA esp32: finalize %s io_ok=%d\n", _full ? "FULL" : "DELTA", (int)_io_ok);
}

// Persist mid-transfer progress so a reboot can resume. Flush the open payload sector (KEEP it buffered so
// continued in-order writes aren't lost), then the meta region (leaves). Payload-before-leaves keeps it
// consistent: every block whose leaf is now in flash also has its payload in flash. Infrequent.
void OtaStoreFlashEsp32::checkpoint() {
  if (_total == 0 || _flushed || !_io_ok) return;
  if (_pay_open) flush_sector((uint32_t)_pay_sec * SEC, _pay, SEC);   // flush but leave _pay/_pay_open intact
  flush_sector(_meta_part, _meta, _meta_flush);                       // header + manifest + leaves (+full trailer)
}

// Re-attach to a container already staged in the slot (after a reboot), WITHOUT erasing. The header
// (MOTA_MAGIC + total) sits at a sector boundary (meta_part for full, write_start for delta — both
// sector-aligned), so scan sector starts from the bottom up. A candidate is accepted only if: magic +
// plausible total, the manifest parses, the container geometry is self-consistent with the total, AND the
// recomputed placement lands the meta exactly where we found the magic. Any miss -> false (fetch fresh),
// so a stray/stale match can only cost a restart, never a corrupt adopt.
bool OtaStoreFlashEsp32::reopen() {
  if (!acquire() || _psize < SEC) return false;
  uint8_t hb[8];
  // the meta/container is staged at the BOTTOM of the slot, so scan upward from there; cap the scan (a
  // miss just means "fetch fresh"). 128 sectors (512 KB) covers any full image's meta + typical deltas.
  uint32_t scanned = 0;
  for (uint32_t o = align_down(_psize - SEC, SEC); ; o -= SEC) {
    if (esp_partition_read(_part, o, hb, 8) == ESP_OK && memcmp(hb, MOTA_MAGIC, 4) == 0) {
      uint32_t total = rd_u32le(hb + 4);
      if (total >= 13 && total <= _psize) {
        uint8_t mbuf[256]; uint32_t mread = total - 8; if (mread > sizeof(mbuf)) mread = sizeof(mbuf);
        MotaManifest m;
        if (esp_partition_read(_part, o + 8, mbuf, mread) == ESP_OK && mota_parse_manifest(mbuf, mread, m)) {
          uint32_t mfl = (uint32_t)(m.approval - m.manifest_start) + 4;
          uint32_t payload_off = 8 + mfl + m.block_count * 4;
          if ((uint64_t)payload_off + m.payload_size + 5 == total) {
            _full = m.is_full(); _image_size = m.image_size; _meta_bytes = payload_off; _pay_size = m.payload_size;
            if (layout() && _meta_part == o) {                 // geometry agrees AND magic is where we'd place meta
              free(_meta); _meta = (uint8_t*)malloc(_meta_flush);
              if (!_meta) { _total = 0; return false; }
              if (esp_partition_read(_part, _meta_part, _meta, _meta_flush) != ESP_OK) {
                free(_meta); _meta = nullptr; _total = 0; return false; }
              memset(_trailer, 0xFF, sizeof(_trailer));        // delta trailer (re-written at finalize); full reads it from _meta
              _pay_open = false; _pay_sec = 0; _flushed = false; _io_ok = true;
              _pay_max_sec = (_pay_part0 + _pay_size + SEC) / SEC;   // treat all payload sectors as seen -> RMW preserves committed blocks
              OTA_DBG("OTA esp32: reopen %s total=%u meta_part=%u\n", _full ? "FULL" : "DELTA", (unsigned)total, (unsigned)o);
              return true;
            }
          }
        }
      }
    }
    if (o == 0 || ++scanned >= 128) break;
  }
  return false;
}

} // namespace ota
} // namespace mesh

#endif
