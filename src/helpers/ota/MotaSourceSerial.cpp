#include "MotaSourceSerial.h"
#include "MotaSeederProto.h"
#include "OtaByteIO.h"
#include <string.h>

namespace mesh {
namespace ota {

bool SerialMotaSource::readByteT(uint8_t& b) {
  uint32_t t0 = millis();
  while ((millis() - t0) < _to) {
    int c = _io.read();
    if (c >= 0) { b = (uint8_t)c; return true; }
  }
  return false;
}

bool SerialMotaSource::readExact(uint8_t* b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) if (!readByteT(b[i])) return false;
  return true;
}

// One request/response transaction. Resync-safe: drains stale input, frames the request with an XOR
// checksum, then scans for the response magic and validates op+status+checksum before delivering payload.
bool SerialMotaSource::txn(uint8_t op, const uint8_t* args, uint8_t arglen,
                           uint8_t* payload, uint32_t payload_len) {
  while (_io.read() >= 0) {}                       // drop any stale/partial bytes before a fresh request
  uint8_t xs = op;
  for (uint8_t i = 0; i < arglen; i++) xs ^= args[i];
  _io.write(MOTA_SEEDER_REQ_MAGIC0); _io.write(MOTA_SEEDER_REQ_MAGIC1);
  _io.write(op);
  if (arglen) _io.write(args, arglen);
  _io.write(xs);
  _io.flush();

  // scan for response magic 'm' 's' (tolerate leading noise)
  uint32_t t0 = millis(); bool got = false;
  uint8_t prev = 0;
  while ((millis() - t0) < _to) {
    int c = _io.read();
    if (c < 0) continue;
    if (prev == MOTA_SEEDER_RSP_MAGIC0 && (uint8_t)c == MOTA_SEEDER_RSP_MAGIC1) { got = true; break; }
    prev = (uint8_t)c;
  }
  if (!got) return false;

  uint8_t hdr[2];
  if (!readExact(hdr, 2)) return false;            // op, status
  if (hdr[0] != op) return false;
  uint8_t rxs = (uint8_t)(MOTA_SEEDER_RSP_MAGIC0 ^ MOTA_SEEDER_RSP_MAGIC1) ^ hdr[0] ^ hdr[1];
  bool ok = (hdr[1] == MS_STATUS_OK);
  if (ok && payload_len) {
    if (!readExact(payload, payload_len)) return false;
    for (uint32_t i = 0; i < payload_len; i++) rxs ^= payload[i];
  }
  uint8_t xsum;
  if (!readByteT(xsum)) return false;
  if (xsum != rxs) return false;                   // corrupt frame -> caller retries
  return ok;
}

uint8_t SerialMotaSource::count() {
  uint8_t n = 0;
  if (!txn(MS_OP_COUNT, nullptr, 0, &n, 1)) return 0;
  return n;
}

bool SerialMotaSource::describe(uint8_t idx, MotaDesc& out) {
  uint8_t args[1] = { idx };
  uint8_t w[MOTA_DESC_WIRE];
  if (!txn(MS_OP_DESCRIBE, args, 1, w, MOTA_DESC_WIRE)) return false;
  memcpy(out.mid, w, 4);
  out.target_id    = rd_u32le(w + 4);
  out.fw_version   = rd_u32le(w + 8);
  out.codec_id     = w[12];
  out.flags        = w[13];
  out.total_size   = rd_u32le(w + 14);
  out.leaves_off   = rd_u32le(w + 18);
  out.block_count  = rd_u32le(w + 22);
  out.payload_off  = rd_u32le(w + 26);
  out.payload_size = rd_u32le(w + 30);
  // bytes [34,38) reserved (zero) — kept for forward compat without changing MOTA_DESC_WIRE
  return true;
}

bool SerialMotaSource::read(uint8_t idx, uint32_t off, uint8_t* buf, uint32_t len) {
  if (len > 0xFFFF) return false;                  // single transaction caps at 64 KB (a block is <=1 KB)
  uint8_t args[7];
  args[0] = idx;
  wr_u32le(args + 1, off);
  args[5] = (uint8_t)(len & 0xFF); args[6] = (uint8_t)(len >> 8);
  return txn(MS_OP_READ, args, 7, buf, len);
}

} // namespace ota
} // namespace mesh
