#include "FolderMotaStore.h"
#include "MotaSeederProto.h"
#include "OtaByteIO.h"
#include <string.h>

namespace mesh {
namespace ota {

bool FolderMotaStore::readByteT(uint8_t& b) const {
  uint32_t t0 = millis();
  while ((millis() - t0) < _to) {
    int c = _io.read();
    if (c >= 0) { b = (uint8_t)c; return true; }
  }
  return false;
}

bool FolderMotaStore::readExact(uint8_t* b, uint16_t n) const {
  for (uint16_t i = 0; i < n; i++) if (!readByteT(b[i])) return false;
  return true;
}

// Resync-safe request/response (same framing as SerialMotaSource): drain stale input, frame op+args with an
// XOR checksum, scan for the response magic, validate op+status+checksum, deliver payload.
bool FolderMotaStore::txn(uint8_t op, const uint8_t* args, uint16_t arglen,
                          uint8_t* payload, uint16_t payload_len) const {
  while (_io.read() >= 0) {}                        // drop any stale/partial bytes before a fresh request
  uint8_t xs = op;
  for (uint16_t i = 0; i < arglen; i++) xs ^= args[i];
  _io.write(MOTA_SEEDER_REQ_MAGIC0); _io.write(MOTA_SEEDER_REQ_MAGIC1);
  _io.write(op);
  if (arglen) _io.write(args, arglen);
  _io.write(xs);
  _io.flush();

  uint32_t t0 = millis(); bool got = false; uint8_t prev = 0;
  while ((millis() - t0) < _to) {                   // scan for response magic 'm' 's' (tolerate noise)
    int c = _io.read();
    if (c < 0) continue;
    if (prev == MOTA_SEEDER_RSP_MAGIC0 && (uint8_t)c == MOTA_SEEDER_RSP_MAGIC1) { got = true; break; }
    prev = (uint8_t)c;
  }
  if (!got) return false;

  uint8_t hdr[2];
  if (!readExact(hdr, 2)) return false;             // op, status
  if (hdr[0] != op) return false;
  uint8_t rxs = (uint8_t)(MOTA_SEEDER_RSP_MAGIC0 ^ MOTA_SEEDER_RSP_MAGIC1) ^ hdr[0] ^ hdr[1];
  bool ok = (hdr[1] == MS_STATUS_OK);
  if (ok && payload_len) {
    if (!readExact(payload, payload_len)) return false;
    for (uint16_t i = 0; i < payload_len; i++) rxs ^= payload[i];
  }
  uint8_t xsum;
  if (!readByteT(xsum)) return false;
  if (xsum != rxs) return false;                    // corrupt frame -> caller retries
  return ok;
}

bool FolderMotaStore::begin(uint32_t total_size) {
  uint8_t args[8];
  memcpy(args, _mid, 4);
  wr_u32le(args + 4, total_size);
  if (!txn(MS_OP_BEGIN, args, 8, nullptr, 0)) return false;
  _total = total_size;
  return true;
}

bool FolderMotaStore::write(uint32_t off, const uint8_t* data, uint32_t len) {
  uint8_t req[10 + MOTA_SEEDER_WRITE_MAX];
  memcpy(req, _mid, 4);
  uint32_t done = 0;
  while (done < len) {
    uint16_t chunk = (len - done > MOTA_SEEDER_WRITE_MAX) ? MOTA_SEEDER_WRITE_MAX : (uint16_t)(len - done);
    wr_u32le(req + 4, off + done);
    req[8] = (uint8_t)(chunk & 0xFF); req[9] = (uint8_t)(chunk >> 8);
    memcpy(req + 10, data + done, chunk);
    if (!txn(MS_OP_WRITE, req, (uint16_t)(10 + chunk), nullptr, 0)) return false;
    done += chunk;
  }
  return true;
}

bool FolderMotaStore::read(uint32_t off, uint8_t* buf, uint32_t len) const {
  uint8_t args[10];
  memcpy(args, _mid, 4);
  uint32_t done = 0;
  while (done < len) {
    uint16_t chunk = (len - done > MOTA_SEEDER_WRITE_MAX) ? MOTA_SEEDER_WRITE_MAX : (uint16_t)(len - done);
    wr_u32le(args + 4, off + done);
    args[8] = (uint8_t)(chunk & 0xFF); args[9] = (uint8_t)(chunk >> 8);
    if (!txn(MS_OP_SREAD, args, 10, buf + done, chunk)) return false;
    done += chunk;
  }
  return true;
}

void FolderMotaStore::finalize() {
  txn(MS_OP_FIN, _mid, 4, nullptr, 0);   // publish <midhex>.mota.part as <midhex>.mota (best-effort)
}

bool FolderMotaStore::reopen() {
  uint8_t pl[5];
  if (!txn(MS_OP_STAT, _mid, 4, pl, 5)) return false;
  if (pl[0] == 0) return false;                     // host has no partial/complete file -> start fresh
  uint32_t total = rd_u32le(pl + 1);
  if (total < 13) return false;                     // implausible container
  _total = total;
  return true;
}

} // namespace ota
} // namespace mesh
