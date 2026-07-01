#include "OtaProtocol.h"
#include <string.h>

namespace mesh {
namespace ota {

// Little-endian cursor helpers.
namespace {
struct W {
  uint8_t* p; uint16_t cap; uint16_t n; bool ok;
  W(uint8_t* b, uint16_t c) : p(b), cap(c), n(0), ok(true) {}
  void u8(uint8_t v)  { if (n + 1 > cap) { ok = false; return; } p[n++] = v; }
  void u16(uint16_t v){ u8(v & 0xFF); u8(v >> 8); }
  void u32(uint32_t v){ u8(v); u8(v >> 8); u8(v >> 16); u8(v >> 24); }
  void raw(const uint8_t* d, uint16_t l) { if (n + l > cap) { ok = false; return; } memcpy(p + n, d, l); n += l; }
};
struct R {
  const uint8_t* p; uint16_t len; uint16_t n; bool ok;
  R(const uint8_t* b, uint16_t l) : p(b), len(l), n(0), ok(true) {}
  uint8_t  u8()  { if (n + 1 > len) { ok = false; return 0; } return p[n++]; }
  uint16_t u16() { uint16_t a = u8(); return a | ((uint16_t)u8() << 8); }
  uint32_t u32() { uint32_t a = u8(); a |= (uint32_t)u8() << 8; a |= (uint32_t)u8() << 16; a |= (uint32_t)u8() << 24; return a; }
  const uint8_t* raw(uint16_t l) { if (n + l > len) { ok = false; return nullptr; } const uint8_t* r = p + n; n += l; return r; }
  uint16_t remaining() const { return len - n; }
};
} // namespace

uint16_t encode_adv(uint8_t* buf, uint16_t cap, const AdvMsg& m) {   // tiny per-node beacon
  W w(buf, cap); w.u8(OTA_ADV); w.raw(m.seeder_id, 4); w.u8(m.n_motas); w.raw(m.set_digest, 4);
  return w.ok ? w.n : 0;
}
bool decode_adv(const uint8_t* buf, uint16_t len, AdvMsg& m) {
  R r(buf, len); if (r.u8() != OTA_ADV) return false;
  const uint8_t* sid = r.raw(4); if (sid) memcpy(m.seeder_id, sid, 4);
  m.n_motas = r.u8();
  const uint8_t* d = r.raw(4); if (d) memcpy(m.set_digest, d, 4);
  return r.ok;
}

uint16_t encode_query(uint8_t* buf, uint16_t cap, const QueryMsg& m) {
  W w(buf, cap); w.u8(OTA_QUERY); w.raw(m.seeder_id, 4); w.raw(m.set_digest, 4); w.u32(m.filter_target);
  return w.ok ? w.n : 0;
}
bool decode_query(const uint8_t* buf, uint16_t len, QueryMsg& m) {
  R r(buf, len); if (r.u8() != OTA_QUERY) return false;
  const uint8_t* sid = r.raw(4); if (sid) memcpy(m.seeder_id, sid, 4);
  const uint8_t* dg = r.raw(4); if (dg) memcpy(m.set_digest, dg, 4);
  m.filter_target = r.u32();
  return r.ok;
}

uint16_t encode_have(uint8_t* buf, uint16_t cap, const HaveMsg& m) {
  W w(buf, cap); w.u8(OTA_HAVE); w.raw(m.seeder_id, 4); w.raw(m.set_digest, 4);
  w.u8(m.frag_idx); w.u8(m.frag_total); w.u8(m.n_rows); w.raw(m.rows, (uint16_t)m.n_rows * OTA_HAVE_ROW_BYTES);
  return w.ok ? w.n : 0;
}
bool decode_have(const uint8_t* buf, uint16_t len, HaveMsg& m) {
  R r(buf, len); if (r.u8() != OTA_HAVE) return false;
  const uint8_t* sid = r.raw(4); if (sid) memcpy(m.seeder_id, sid, 4);
  const uint8_t* dg = r.raw(4); if (dg) memcpy(m.set_digest, dg, 4);
  m.frag_idx = r.u8(); m.frag_total = r.u8(); m.n_rows = r.u8();
  m.rows = r.raw((uint16_t)m.n_rows * OTA_HAVE_ROW_BYTES);
  return r.ok;
}

uint16_t encode_get_manifest(uint8_t* buf, uint16_t cap, const GetManifestMsg& m) {
  W w(buf, cap); w.u8(OTA_GET_MANIFEST); w.raw(m.manifest_id, 4);
  return w.ok ? w.n : 0;
}
bool decode_get_manifest(const uint8_t* buf, uint16_t len, GetManifestMsg& m) {
  R r(buf, len); if (r.u8() != OTA_GET_MANIFEST) return false;
  const uint8_t* id = r.raw(4); if (id) memcpy(m.manifest_id, id, 4);
  return r.ok;
}

uint16_t encode_manifest(uint8_t* buf, uint16_t cap, const ManifestMsg& m) {
  W w(buf, cap); w.u8(OTA_MANIFEST); w.raw(m.manifest_id, 4); w.u8(m.frag_idx); w.u8(m.frag_total);
  w.raw(m.bytes, m.len);
  return w.ok ? w.n : 0;
}
bool decode_manifest(const uint8_t* buf, uint16_t len, ManifestMsg& m) {
  R r(buf, len); if (r.u8() != OTA_MANIFEST) return false;
  const uint8_t* id = r.raw(4); if (id) memcpy(m.manifest_id, id, 4);
  m.frag_idx = r.u8(); m.frag_total = r.u8();
  m.len = r.remaining(); m.bytes = r.raw(m.len);
  return r.ok;
}

uint16_t encode_req(uint8_t* buf, uint16_t cap, const ReqMsg& m) {
  W w(buf, cap); w.u8(OTA_REQ); w.raw(m.manifest_id, 4); w.u16(m.start_block); w.u8(m.count);
  return w.ok ? w.n : 0;
}
bool decode_req(const uint8_t* buf, uint16_t len, ReqMsg& m) {
  R r(buf, len); if (r.u8() != OTA_REQ) return false;
  const uint8_t* id = r.raw(4); if (id) memcpy(m.manifest_id, id, 4);
  m.start_block = r.u16(); m.count = r.u8();
  return r.ok;
}

uint16_t encode_data(uint8_t* buf, uint16_t cap, const DataMsg& m) {
  W w(buf, cap); w.u8(OTA_DATA); w.raw(m.manifest_id, 4); w.u16(m.block_idx); w.u16(m.frag_off);
  w.raw(m.data, m.data_len);
  return w.ok ? w.n : 0;
}
bool decode_data(const uint8_t* buf, uint16_t len, DataMsg& m) {
  R r(buf, len); if (r.u8() != OTA_DATA) return false;
  const uint8_t* id = r.raw(4); if (id) memcpy(m.manifest_id, id, 4);
  m.block_idx = r.u16(); m.frag_off = r.u16();
  m.data_len = r.remaining(); m.data = r.raw(m.data_len);
  return r.ok;
}

uint16_t encode_req_proof(uint8_t* buf, uint16_t cap, const ReqProofMsg& m) {
  W w(buf, cap); w.u8(OTA_REQ_PROOF); w.raw(m.manifest_id, 4); w.u16(m.block_idx);
  return w.ok ? w.n : 0;
}
bool decode_req_proof(const uint8_t* buf, uint16_t len, ReqProofMsg& m) {
  R r(buf, len); if (r.u8() != OTA_REQ_PROOF) return false;
  const uint8_t* id = r.raw(4); if (id) memcpy(m.manifest_id, id, 4);
  m.block_idx = r.u16();
  return r.ok;
}

uint16_t encode_proof(uint8_t* buf, uint16_t cap, const ProofMsg& m) {
  W w(buf, cap); w.u8(OTA_PROOF); w.raw(m.manifest_id, 4); w.u16(m.block_idx);
  w.u8(m.n_proof); w.raw(m.proof, (uint16_t)m.n_proof * 4);
  return w.ok ? w.n : 0;
}
bool decode_proof(const uint8_t* buf, uint16_t len, ProofMsg& m) {
  R r(buf, len); if (r.u8() != OTA_PROOF) return false;
  const uint8_t* id = r.raw(4); if (id) memcpy(m.manifest_id, id, 4);
  m.block_idx = r.u16(); m.n_proof = r.u8(); m.proof = r.raw((uint16_t)m.n_proof * 4);
  return r.ok;
}

} // namespace ota
} // namespace mesh
