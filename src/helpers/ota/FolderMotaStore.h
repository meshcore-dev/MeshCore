#pragma once

#include <Arduino.h>
#include "OtaStore.h"

// An OtaStore that captures an in-transit `.mota` onto a HOST folder over the mota-seeder link (the WRITE
// half of MotaSeederProto: OP_STAT/BEGIN/WRITE/SREAD/FIN). This is the destination for `ota pull <#> folder`:
// blocks stream straight to the host `<mid>.mota` — the device holds NO RAM/flash staging for it.
//
//   begin(total)      -> OP_BEGIN  (host creates a 0xFF-filled <midhex>.mota.part)
//   write(off,data)   -> OP_WRITE  (split into <= MOTA_SEEDER_WRITE_MAX chunks)
//   read(off,buf)     -> OP_SREAD  (read back; unwritten regions are 0xFF)
//   reopen()          -> OP_STAT   (adopt an existing partial/complete file for resume)
//   finalize()        -> OP_FIN    (publish <midhex>.mota.part as <midhex>.mota)
//
// If the link drops, every op returns false: the OtaManager PAUSES and keeps its progress (it does NOT fall
// back to RAM/flash). On reconnect the host still has the partial, and the manager re-reopen()s so only the
// missing blocks are refetched. All ops are keyed by `mid` (set via set_mid() before the fetch begins).

namespace mesh {
namespace ota {

class FolderMotaStore : public OtaStore {
public:
  explicit FolderMotaStore(Stream& io, uint32_t timeout_ms = 3000) : _io(io), _to(timeout_ms) {}

  // The container being pulled — set from the chosen `ota pull` mid before begin()/reopen().
  void set_mid(const uint8_t mid[4]) { memcpy(_mid, mid, 4); }

  bool begin(uint32_t total_size) override;
  bool write(uint32_t off, const uint8_t* data, uint32_t len) override;
  bool read(uint32_t off, uint8_t* buf, uint32_t len) const override;
  uint32_t capacity() const override { return 0xF0000000u; }   // host disk — effectively unbounded
  uint32_t staged_size() const override { return _total; }
  void clear() override { _total = 0; }
  void finalize() override;
  bool reopen() override;                                       // OP_STAT: adopt an existing file for resume

  // A host-backed store is fully random-access, so it needs no pinned-meta RAM page and no layout planning.
  bool set_meta_size(uint32_t) override { return true; }
  bool plan_layout(bool, uint32_t, uint32_t, uint32_t) override { return true; }

private:
  // One request/response transaction over the seeder link (mirrors SerialMotaSource). `arglen` is uint16 so
  // OP_WRITE can carry its data inline. Returns true iff a well-formed OK response for `op` arrived in time.
  bool txn(uint8_t op, const uint8_t* args, uint16_t arglen, uint8_t* payload, uint16_t payload_len) const;
  bool readByteT(uint8_t& b) const;
  bool readExact(uint8_t* b, uint16_t n) const;

  Stream&  _io;
  uint32_t _to;
  uint8_t  _mid[4] = {0, 0, 0, 0};
  uint32_t _total = 0;
};

} // namespace ota
} // namespace mesh
