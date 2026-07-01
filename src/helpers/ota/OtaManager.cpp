#include "OtaManager.h"
#include "OtaProtocol.h"
#include "MerkleTree.h"
#include "Multihash.h"
#include "OtaByteIO.h"
#include "OtaDebug.h"
#include <string.h>

namespace mesh {
namespace ota {

void OtaManager::begin(uint32_t my_target_id, OtaSend send, void* ctx) {
  _target = my_target_id; _send = send; _ctx = ctx;
  _fstate = IDLE; _have = 0; _fbc = 0;
  _n_serve = 0; _n_src_obj = 0; _view0.valid = false; _srcv.valid = false; _fetch_served = false;
  for (uint8_t i = 0; i < 8; i++) _recent_blk[i] = NO_BLOCK;      // empty slot (never a real block index)
}

// ---------------- serve (multi-mota registry) ----------------
//
// A node offers a SET of mOTAs: its own firmware (view0) plus any external "folder" sources (OtaSource).
// Every fetch message carries the manifest_id, so a request dispatches to the matching ServeView via
// resolve() — view0 is resident; an external mota is (re)loaded on demand into _srcv. The catalog (what
// we advertise / answer OTA_QUERY with) is the lightweight _serve[] registry.

bool OtaManager::serve(const uint8_t* mota, uint32_t len) {
  if (!mota_parse(mota, len, _view0.m)) return false;
  _view0.mfl = (uint16_t)(_view0.m.leaves - _view0.m.manifest_start);  // contiguous container
  _view0.read = nullptr; _view0.read_ctx = nullptr;                    // payload is contiguous _view0.m.payload
  _view0.scratch = _scratch; _view0.scratch_sz = sizeof(_scratch);     // <=1024 blocks (RAM .mota is small)
  _view0.valid = true;
  registerSelfEntry();
  return true;
}

bool OtaManager::serve_self(const uint8_t* manifest, uint16_t mfl, const uint8_t* leaves,
                            uint32_t block_count, uint8_t* proof_scratch, uint32_t proof_scratch_sz,
                            ServeReadFn read, void* ctx) {
  if (proof_scratch_sz < (uint64_t)block_count * 4) return false;   // proof-gen needs count*4 working bytes
  if (!mota_parse_manifest(manifest, mfl, _view0.m)) return false;  // fixed fields: root, image_hash, sizes
  _view0.m.manifest_start = manifest;
  _view0.m.leaves   = leaves;        // pre-computed, caller-owned (heap)
  _view0.m.payload  = nullptr;       // read on demand via `read`
  _view0.m.block_count = block_count;
  _view0.mfl = mfl; _view0.read = read; _view0.read_ctx = ctx;
  _view0.scratch = proof_scratch; _view0.scratch_sz = proof_scratch_sz;   // sized for our (large) image
  _view0.valid = true;
  registerSelfEntry();
  return true;
}

// (Re)build registry slot 0 from view0 (our own fw / RAM mota). Keeps any source entries in [1..].
void OtaManager::registerSelfEntry() {
  if (!_view0.valid) return;
  ServeEntry& e = _serve[0];
  memcpy(e.mid, _view0.m.merkle_root, 4);
  e.target_id = _view0.m.target_id; e.fw_version = _view0.m.fw_version;
  e.codec_id = _view0.m.codec_id; e.flags = _view0.m.flags; e.have_count = _view0.m.block_count;
  e.is_self = true; e.is_fetch = false; e.src = nullptr; e.src_idx = 0;
  if (_n_serve == 0) _n_serve = 1;
}

bool OtaManager::add_source(MotaSource* src) {
  if (!src || _n_src_obj >= OTA_MAX_SOURCE_OBJ) return false;
  _src_list[_n_src_obj++] = src;
  refresh_sources();
  return true;
}

void OtaManager::refresh_sources() {
  uint8_t base = _view0.valid ? 1 : 0;       // entry 0 stays our own fw
  if (_view0.valid) registerSelfEntry();
  _n_serve = base;
  for (uint8_t s = 0; s < _n_src_obj; s++) {
    MotaSource* src = _src_list[s];
    if (!src) continue;
    uint8_t cnt = src->count();
    for (uint8_t i = 0; i < cnt && _n_serve < OTA_MAX_SERVE; i++) {
      MotaDesc d;
      if (!src->describe(i, d)) continue;
      if (serveEntryIndex(d.mid) >= 0) continue;             // already offered (e.g. our own fw in the folder)
      ServeEntry& e = _serve[_n_serve++];
      memcpy(e.mid, d.mid, 4);
      e.target_id = d.target_id; e.fw_version = d.fw_version;
      e.codec_id = d.codec_id; e.flags = d.flags; e.have_count = d.block_count;   // a folder mota is fully held
      e.is_self = false; e.is_fetch = false; e.src = src; e.src_idx = i; e.desc = d;
    }
  }
  // re-seed a completed download (epidemic spread) as one more served mota, backed by the fetch store
  if (_fetch_served && _n_serve < OTA_MAX_SERVE && serveEntryIndex(_fetch_desc.mid) < 0) {
    ServeEntry& e = _serve[_n_serve++];
    memcpy(e.mid, _fetch_desc.mid, 4);
    e.target_id = _fetch_desc.target_id; e.fw_version = _fetch_desc.fw_version;
    e.codec_id = _fetch_desc.codec_id; e.flags = _fetch_desc.flags; e.have_count = _fetch_desc.block_count;
    e.is_self = false; e.is_fetch = true; e.src = nullptr; e.src_idx = 0; e.desc = _fetch_desc;
  }
  _srcv.valid = false;                        // a loaded source view may now be stale; reloads on demand
}

void OtaManager::clear_sources() {
  _n_src_obj = 0; _srcv.valid = false;
  _n_serve = _view0.valid ? 1 : 0;
  if (_view0.valid) registerSelfEntry();
}

int OtaManager::serveEntryIndex(const uint8_t* mid) const {
  for (uint8_t i = 0; i < _n_serve; i++)
    if (memcmp(_serve[i].mid, mid, 4) == 0) return i;
  return -1;
}

OtaManager::ServeView* OtaManager::resolve(const uint8_t* mid) {
  if (_view0.valid && memcmp(mid, _view0.m.merkle_root, 4) == 0) return &_view0;
  if (_srcv.valid  && memcmp(mid, _srcv_mid, 4) == 0)             return &_srcv;
  int i = serveEntryIndex(mid);
  if (i < 0) return nullptr;
  if (_serve[i].is_self) return _view0.valid ? &_view0 : nullptr;
  return loadSource(_serve[i]) ? &_srcv : nullptr;
}

// Load an external mota into the on-demand _srcv: read its manifest-minus-leaves + leaves[] from the
// source into RAM, parse, and wire a payload reader that streams blocks from the source on REQ. (The
// payload itself is NOT held in RAM — only the small head + the leaves, <=4 KB for <=1024 blocks.)
bool OtaManager::loadSource(const ServeEntry& e) {
  const MotaDesc& d = e.desc;
  if (d.leaves_off < 8) return false;
  if (e.is_fetch ? (_fetch == nullptr) : (e.src == nullptr)) return false;
  uint16_t mfl = (uint16_t)(d.leaves_off - 8);
  if (mfl == 0 || mfl > sizeof(_src_manifest)) return false;
  if (d.block_count == 0 || (uint64_t)d.block_count * 4 > sizeof(_src_leaves)) return false;
  // read the manifest-minus-leaves + leaves[] from the backing — an external folder MotaSource, or (for a
  // completed download we re-seed) our own fetch store. Container offsets are absolute, so a store read
  // at the same offsets works identically.
  bool ok = e.is_fetch ? _fetch->read(8, _src_manifest, mfl)
                       : e.src->read(e.src_idx, 8, _src_manifest, mfl);
  if (!ok || !mota_parse_manifest(_src_manifest, mfl, _srcv.m)) return false;
  if (memcmp(_srcv.m.merkle_root, d.mid, 4) != 0) return false;       // descriptor/bytes disagree
  if (_srcv.m.block_count != d.block_count) return false;
  ok = e.is_fetch ? _fetch->read(d.leaves_off, _src_leaves, d.block_count * 4)
                  : e.src->read(e.src_idx, d.leaves_off, _src_leaves, d.block_count * 4);
  if (!ok) return false;
  _srcv.m.manifest_start = _src_manifest;
  _srcv.m.leaves   = _src_leaves;
  _srcv.m.payload  = nullptr;
  _srcv.mfl = mfl;
  _srcv_rdctx.src = e.is_fetch ? nullptr : e.src; _srcv_rdctx.idx = e.src_idx;
  _srcv_rdctx.payload_off = d.payload_off; _srcv_rdctx.store = e.is_fetch ? _fetch : nullptr;
  _srcv.read = srcReadTramp; _srcv.read_ctx = &_srcv_rdctx;
  _srcv.scratch = _scratch; _srcv.scratch_sz = sizeof(_scratch);
  memcpy(_srcv_mid, d.mid, 4);
  _srcv.valid = true;
  return true;
}

// ServeReadFn trampoline: payload-relative offset -> absolute read of the backing (external source or fetch store).
bool OtaManager::srcReadTramp(void* c, uint32_t off, uint8_t* buf, uint32_t len) {
  SrcReadCtx* x = (SrcReadCtx*)c;
  if (x->store) return x->store->read(x->payload_off + off, buf, len);
  return x->src->read(x->idx, x->payload_off + off, buf, len);
}

// After a download COMPLETEs, advertise + serve the staged container so this node re-seeds it to peers
// (epidemic spread: the origin seeds a few, they seed the next ring -> load on the origin is O(log N), not
// O(N)). The completed container has ALL blocks + leaves, so it serves DATA *and* proofs correctly. Re-uses
// the on-demand source view; serving is reactive + lowest-priority, so it never competes with real traffic.
void OtaManager::serveFetched() {
  if (!_fetch || _fstate != COMPLETE || _fbc == 0 || _floff < 8) return;
  uint16_t mfl = (uint16_t)(_floff - 8);
  if (mfl == 0 || mfl > sizeof(_src_manifest)) return;
  if ((uint64_t)_fbc * 4 > sizeof(_src_leaves)) return;     // proof-gen scratch caps re-seed at <=1024 blocks
  uint8_t head[OTA_SRC_MANIFEST_MAX];
  if (!_fetch->read(8, head, mfl)) return;
  MotaManifest m;
  if (!mota_parse_manifest(head, mfl, m)) return;
  MotaDesc& d = _fetch_desc;
  memcpy(d.mid, _fid, 4);
  d.target_id = m.target_id; d.fw_version = m.fw_version; d.codec_id = m.codec_id; d.flags = m.flags;
  d.total_size = _ftotal; d.leaves_off = _floff; d.block_count = _fbc;
  d.payload_off = _fpoff; d.payload_size = _fpsize;
  _fetch_served = true;
  refresh_sources();        // add the fetch entry to the catalog; the set-digest change makes the next beacon advertise it
}

void OtaManager::unserveFetched() {
  if (!_fetch_served) return;
  _fetch_served = false;
  _srcv.valid = false;      // the loaded source view may be the fetch we're dropping
  refresh_sources();
}

// sha2-256:4 over the SORTED set of mids we serve — peers use it to tell if our offering changed. Sorting
// makes it canonical across nodes regardless of insert order; for a single mota it is mh4(mid) (unchanged).
void OtaManager::setDigest(uint8_t out[4]) const {
  if (_n_serve == 0) { memset(out, 0, 4); return; }
  uint8_t order[OTA_MAX_SERVE];
  for (uint8_t i = 0; i < _n_serve; i++) order[i] = i;
  for (uint8_t i = 1; i < _n_serve; i++) {                 // insertion sort by mid (n <= 12)
    uint8_t v = order[i]; int j = (int)i - 1;
    while (j >= 0 && memcmp(_serve[order[j]].mid, _serve[v].mid, 4) > 0) { order[j+1] = order[j]; j--; }
    order[j+1] = v;
  }
  uint8_t cat[OTA_MAX_SERVE * 4];
  for (uint8_t i = 0; i < _n_serve; i++) memcpy(cat + (uint32_t)i * 4, _serve[order[i]].mid, 4);
  mh4(out, cat, (size_t)_n_serve * 4);
}

void OtaManager::announce() {       // tiny per-node beacon (constant size, independent of how many mOTAs)
  AdvMsg a;
  memcpy(a.seeder_id, _seeder_id, 4);
  a.n_motas = _n_serve;
  setDigest(a.set_digest);
  uint8_t b[16];
  emit(b, encode_adv(b, sizeof(b), a), true);
}

// OTA_QUERY: two roles. (1) OVERHEAR-SUPPRESSION — any node that has a pending query for the same
// {source,digest} cancels it (someone else already asked; the broadcast HAVE is coming). (2) If the query
// is addressed to US, reply with our catalog (broadcast, tagged with our digest so every overhearer caches
// it). All served mOTAs matching filter_target are returned, fragmented if they exceed one packet.
void OtaManager::handleQuery(const uint8_t* m, uint16_t n) {
  QueryMsg q;
  if (!decode_query(m, n, q)) return;
  if (_pq_active && memcmp(_pq_seeder, q.seeder_id, 4) == 0 && memcmp(_pq_digest, q.set_digest, 4) == 0)
    _pq_active = false;                                      // (1) suppress our own pending query
  if (_n_serve == 0 || memcmp(q.seeder_id, _seeder_id, 4) != 0) return;   // (2) only WE answer queries to us
  uint8_t dg[4]; setDigest(dg);
  uint8_t rowbuf[OTA_MAX_SERVE * OTA_HAVE_ROW_BYTES];
  uint8_t nm = 0;
  for (uint8_t i = 0; i < _n_serve; i++) {
    const ServeEntry& e = _serve[i];
    if (q.filter_target != 0 && q.filter_target != e.target_id) continue;
    uint8_t* row = rowbuf + (uint32_t)nm * OTA_HAVE_ROW_BYTES;
    memcpy(row, e.mid, 4);
    wr_u32le(row + 4, e.target_id); wr_u32le(row + 8, e.fw_version);
    row[12] = e.codec_id; row[13] = e.flags;
    uint32_t hc = e.have_count > 0xFFFFu ? 0xFFFFu : e.have_count;   // blocks we hold (awareness for fetchers)
    row[14] = (uint8_t)(hc & 0xFF); row[15] = (uint8_t)(hc >> 8);
    nm++;
  }
  const uint8_t per = (uint8_t)((MAX_PACKET_PAYLOAD - 12) / OTA_HAVE_ROW_BYTES);  // rows per HAVE fragment
  uint8_t ftotal = (uint8_t)((nm + per - 1) / per); if (ftotal == 0) ftotal = 1;
  for (uint8_t fi = 0; fi < ftotal; fi++) {
    uint8_t bse = (uint8_t)(fi * per);
    uint8_t cnt = (uint8_t)((nm - bse > per) ? per : (nm - bse));
    HaveMsg hv; memcpy(hv.seeder_id, _seeder_id, 4); memcpy(hv.set_digest, dg, 4);
    hv.frag_idx = fi; hv.frag_total = ftotal; hv.n_rows = cnt;
    hv.rows = cnt ? (rowbuf + (uint32_t)bse * OTA_HAVE_ROW_BYTES) : nullptr;
    uint8_t b[MAX_PACKET_PAYLOAD];
    emit(b, encode_have(b, sizeof(b), hv), true);            // broadcast: all neighbours cache it
  }
}

void OtaManager::handleGetManifest(const uint8_t* m, uint16_t n) {
  GetManifestMsg gm;
  if (!decode_get_manifest(m, n, gm)) return;
  ServeView* v = resolve(gm.manifest_id);
  if (!v) return;
  // A signed v2 manifest (with hw_id[32]) exceeds one LoRa packet, so send it as fragments. Each carries
  // up to OTA_MF_FRAG manifest bytes; the client reassembles by frag_idx. (Re-sent in full on a retry.)
  uint32_t mfl = v->mfl;
  const uint8_t* src = v->m.manifest_start;
  uint8_t ftotal = (uint8_t)((mfl + OTA_MF_FRAG - 1) / OTA_MF_FRAG); if (ftotal == 0) ftotal = 1;
  for (uint8_t fi = 0; fi < ftotal; fi++) {
    uint32_t off = (uint32_t)fi * OTA_MF_FRAG;
    uint32_t fl = mfl - off; if (fl > OTA_MF_FRAG) fl = OTA_MF_FRAG;
    ManifestMsg mm;
    memcpy(mm.manifest_id, v->m.merkle_root, 4);
    mm.frag_idx = fi; mm.frag_total = ftotal;
    mm.bytes = src + off; mm.len = (uint16_t)fl;
    uint8_t b[MAX_PACKET_PAYLOAD];
    emit(b, encode_manifest(b, sizeof(b), mm), false);
  }
}

// Emit one block's data as self-describing DATA fragments (frag_off); the proof is fetched separately.
void OtaManager::emitBlockData(const uint8_t* mid, uint32_t idx, const uint8_t* data, uint32_t blen) {
  for (uint32_t fo = 0; fo < blen; fo += OTA_FRAG_DATA) {
    uint32_t fl = (fo + OTA_FRAG_DATA <= blen) ? OTA_FRAG_DATA : (blen - fo);
    DataMsg dm;
    memcpy(dm.manifest_id, mid, 4);
    dm.block_idx = (uint16_t)idx; dm.frag_off = (uint16_t)fo;
    dm.data = data + fo; dm.data_len = (uint16_t)fl;
    uint8_t b[MAX_PACKET_PAYLOAD];
    emit(b, encode_data(b, sizeof(b), dm), false);
  }
}

// True if we recently overheard ANOTHER holder broadcast this block's DATA — so we should not re-serve it
// (avoids N sources duplicate-broadcasting one block; keeps OTA airtime minimal). See noteOverheardData().
bool OtaManager::recentlyServed(uint32_t blk) const {
  for (uint8_t i = 0; i < 8; i++)
    if (_recent_blk[i] == blk && (uint32_t)(_now_ms - _recent_at[i]) < OTA_SERVE_SUPPRESS_MS) return true;
  return false;
}

void OtaManager::noteOverheardData(const uint8_t* m, uint16_t n) {
  DataMsg dm;
  if (!decode_data(m, n, dm)) return;
  _recent_blk[_recent_i] = dm.block_idx; _recent_at[_recent_i] = _now_ms;
  _recent_i = (uint8_t)((_recent_i + 1) & 7);
}

void OtaManager::handleReq(const uint8_t* m, uint16_t n) {
  ReqMsg rq;
  if (!decode_req(m, n, rq)) return;
  ServeView* v = resolve(rq.manifest_id);
  if (v) {                                          // serve a fully-held mota (own fw / folder / completed fetch)
    uint32_t bs = v->m.block_size();
    for (uint32_t k = 0; k < rq.count; k++) {
      uint32_t idx = rq.start_block + k;
      if (idx >= v->m.block_count) break;
      if (recentlyServed(idx)) continue;            // another holder just broadcast it — don't duplicate
      uint32_t off = idx * bs;
      uint32_t blen = (off + bs <= v->m.payload_size) ? bs : (v->m.payload_size - off);
      uint8_t blk[OTA_MAX_BLOCK];
      const uint8_t* data;
      if (v->read) { if (!v->read(v->read_ctx, off, blk, blen)) break; data = blk; }
      else         { data = v->m.payload + off; }
      emitBlockData(v->m.merkle_root, idx, data, blen);
    }
    return;
  }
  // Partial re-serve (swarm DURING the transfer): we're fetching this mid and already hold some of these
  // blocks — serve their DATA (not proofs; we may lack sibling leaves) from our staging store, so peers can
  // source from us, not only the origin. Reactive + lowest-priority, so real traffic is never impacted.
  if (_fetch && _fstate == FETCHING && memcmp(rq.manifest_id, _fid, 4) == 0) {
    for (uint32_t k = 0; k < rq.count; k++) {
      uint32_t idx = rq.start_block + k;
      if (idx >= _fbc) break;
      if (!blockPresent(idx) || recentlyServed(idx)) continue;
      uint8_t blk[OTA_MAX_BLOCK];
      uint32_t blen = blockLen(idx);
      if (!_fetch->read(_fpoff + idx * _fbs, blk, blen)) continue;
      emitBlockData(_fid, idx, blk, blen);
    }
  }
}

void OtaManager::handleReqProof(const uint8_t* m, uint16_t n) {
  ReqProofMsg rp;
  if (!decode_req_proof(m, n, rp)) return;
  ServeView* v = resolve(rp.manifest_id);
  if (!v) return;
  if (rp.block_idx >= v->m.block_count) return;
  if ((uint64_t)v->m.block_count * 4 > v->scratch_sz) return;     // proof-gen needs block_count*4 scratch
  uint8_t proof[32 * 4];
  uint8_t np = merkle_gen_proof(v->m.leaves, v->m.block_count, rp.block_idx, v->scratch, proof);
  ProofMsg pm;
  memcpy(pm.manifest_id, v->m.merkle_root, 4);
  pm.block_idx = rp.block_idx; pm.n_proof = np; pm.proof = proof;
  uint8_t b[MAX_PACKET_PAYLOAD];
  emit(b, encode_proof(b, sizeof(b), pm), false);
}

// ---------------- fetch ----------------

// A tiny per-node BEACON: record the source; ask it for its catalog (OTA_QUERY) only when we're
// interested AND its set-digest is one we haven't catalogued yet (so a stable mesh is query-free).
void OtaManager::handleAdv(const uint8_t* m, uint16_t n) {
  AdvMsg a;
  if (!decode_adv(m, n, a)) return;
  bool have_sid = (_seeder_id[0] | _seeder_id[1] | _seeder_id[2] | _seeder_id[3]) != 0;
  if (have_sid && memcmp(a.seeder_id, _seeder_id, 4) == 0) return;   // our own beacon, re-flooded
  if (a.n_motas == 0) return;                                        // source offers nothing

  int slot = -1, lru = 0;                                            // find/insert the source (LRU evict)
  for (int i = 0; i < _n_src; i++) {
    if (memcmp(_sources[i].seeder, a.seeder_id, 4) == 0) { slot = i; break; }
    if (_sources[i].last_ms < _sources[lru].last_ms) lru = i;
  }
  bool fresh = (slot < 0);
  if (fresh) { slot = (_n_src < OTA_MAX_SOURCES) ? _n_src++ : lru; _sources[slot] = Source{}; }
  Source& s = _sources[slot];
  bool changed = fresh || memcmp(s.digest, a.set_digest, 4) != 0;
  memcpy(s.seeder, a.seeder_id, 4); memcpy(s.digest, a.set_digest, 4);
  s.n_motas = a.n_motas; s.last_ms = _now_ms;
  if (changed) s.have_catalog = false;

  // interested = auto-fetch enabled, or a manual pull/want is pending. (Browsing queries via queryAll().)
  bool interested = (_autofetch != AUTOFETCH_OFF) || _have_desired_mid || _desired_target;
  if (interested && !s.have_catalog) scheduleQuery(a.seeder_id, a.set_digest);  // jittered + suppressible
}

// Schedule a catalog query after a random jitter (id ⊕ digest, so neighbours pick different delays). The
// node with the shortest jitter sends; the rest overhear that QUERY (or the broadcast HAVE) and suppress.
void OtaManager::scheduleQuery(const uint8_t* seeder, const uint8_t* digest) {
  if (_pq_active && memcmp(_pq_seeder, seeder, 4) == 0 && memcmp(_pq_digest, digest, 4) == 0) return;  // already pending
  memcpy(_pq_seeder, seeder, 4); memcpy(_pq_digest, digest, 4);
  uint32_t j = (rd_u32le(seeder) ^ rd_u32le(digest) ^ rd_u32le(_seeder_id)) % OTA_QUERY_SPREAD_MS;
  _pq_at = _now_ms + OTA_QUERY_MIN_MS + j;
  _pq_active = true;
}

void OtaManager::sendQuery(const uint8_t* seeder, const uint8_t* digest, uint32_t filter_target) {
  QueryMsg q; memcpy(q.seeder_id, seeder, 4); memcpy(q.set_digest, digest, 4); q.filter_target = filter_target;
  uint8_t b[16];
  emit(b, encode_query(b, sizeof(b), q), true);     // FLOODED so neighbours overhear it and suppress
}

// User-initiated browse (`ota neighbors`): ask every known source now (no jitter — infrequent + explicit).
void OtaManager::queryAll() { for (uint8_t i = 0; i < _n_src; i++) sendQuery(_sources[i].seeder, _sources[i].digest, 0); }

// A catalog reply: record each mOTA (deduped by mid; distinct-source count for the UI), and if a row
// matches our fetch interest (auto-fetch own-target, or a pending pull/want), begin fetching it.
void OtaManager::handleHave(const uint8_t* m, uint16_t n) {
  HaveMsg hv;
  if (!decode_have(m, n, hv)) return;
  bool have_sid = (_seeder_id[0] | _seeder_id[1] | _seeder_id[2] | _seeder_id[3]) != 0;
  if (have_sid && memcmp(hv.seeder_id, _seeder_id, 4) == 0) return;   // our own catalog
  // PASSIVE: any overheard HAVE marks its source catalogued + cancels a pending query for it (storm
  // suppression) — every node caches the rows below, even one that never queried.
  for (uint8_t i = 0; i < _n_src; i++)
    if (memcmp(_sources[i].seeder, hv.seeder_id, 4) == 0 && memcmp(_sources[i].digest, hv.set_digest, 4) == 0)
      _sources[i].have_catalog = true;
  if (_pq_active && memcmp(_pq_seeder, hv.seeder_id, 4) == 0 && memcmp(_pq_digest, hv.set_digest, 4) == 0)
    _pq_active = false;
  for (uint8_t r = 0; r < hv.n_rows && hv.rows; r++) {
    const uint8_t* row = hv.rows + (uint32_t)r * OTA_HAVE_ROW_BYTES;
    const uint8_t* mid = row;
    uint32_t target = rd_u32le(row + 4), fwver = rd_u32le(row + 8);
    uint8_t codec = row[12], flags = row[13];
    uint32_t have_count = rd_u16le(row + 14);   // this source's progress
    int slot = -1, lru = 0;                                           // upsert into the catalog (dedup by mid)
    for (int i = 0; i < _n_cat; i++) {
      if (memcmp(_catalog[i].mid, mid, 4) == 0) { slot = i; break; }
      if (_catalog[i].last_ms < _catalog[lru].last_ms) lru = i;
    }
    if (slot < 0) {
      slot = (_n_cat < OTA_MAX_CATALOG) ? _n_cat++ : lru;
      _catalog[slot] = CatRow{};
      memcpy(_catalog[slot].mid, mid, 4);
      memcpy(_catalog[slot].seeders[0], hv.seeder_id, 4);
      _catalog[slot].n_seeders = 1;
    } else {
      CatRow& cc = _catalog[slot];                                   // count DISTINCT sources (no double-count)
      bool known = false;
      for (uint8_t k = 0; k < cc.n_seeders; k++)
        if (memcmp(cc.seeders[k], hv.seeder_id, 4) == 0) { known = true; break; }
      if (!known && cc.n_seeders < OTA_CAT_SEEDERS) memcpy(cc.seeders[cc.n_seeders++], hv.seeder_id, 4);
    }
    CatRow& c = _catalog[slot];
    c.target_id = target; c.fw_version = fwver; c.codec = codec; c.flags = flags; c.last_ms = _now_ms;
    if (have_count > c.have_max) c.have_max = have_count;             // best-known progress among sources
    if (wantRow(mid, target, codec, flags)) startFetch(mid, target);
  }
}

bool OtaManager::wantRow(const uint8_t* mid, uint32_t target, uint8_t codec, uint8_t flags) const {
  if (!_fetch || _fstate == FETCHING || _fstate == WANT_MANIFEST) return false;   // busy with a session
  if (_fstate == COMPLETE && memcmp(mid, _fid, 4) == 0) return false;             // already have it
  if (!codecOk(codec)) return false;                                              // can't apply this codec
  if (_have_desired_mid)                                                          // manual pull of a specific mid
    return memcmp(mid, _desired_mid, 4) == 0 && (_desired_target == 0 || target == _desired_target);
  if (_desired_target) return target == _desired_target;                          // cross-target want (role switch)
  if (_autofetch == AUTOFETCH_OFF) return false;                                  // discover only
  if (target != _target) return false;                                            // auto-fetch = our own target
  if (_autofetch == AUTOFETCH_SIGNED && !(flags & MFLAG_SIGNED)) return false;    // signed-only policy
  return true;
}

// Forget the block currently being reassembled / awaited (back to NO_BLOCK). Safe to call between blocks:
// the next DATA fragment re-derives the slice mask for whatever block it belongs to.
void OtaManager::clearReassembly() {
  _reasm_block = NO_BLOCK; _reasm_mask = 0; _reasm_need = 0; _awaiting_proof = false;
}

// De-sync the first REQ across the swarm: hold it a random fraction of OTA_REQ_SPREAD_MS so N nodes that
// just discovered the same mid don't burst-request block 0 in lockstep (loop() fires it once the hold
// elapses). Assumes the per-node RNG is already seeded; also forgets any peer-REQ note from a prior session.
void OtaManager::armFirstReqHold() {
  _req_hold_at = _now_ms + (rngNext() % OTA_REQ_SPREAD_MS);
  _peer_req_block = NO_BLOCK; _peer_req_at = 0;
}

// Begin (or resume) fetching a chosen mid: try a staged-partial resume first, else request the manifest.
void OtaManager::startFetch(const uint8_t* mid, uint32_t target) {
  (void)target;
  if (!_fetch || _fstate == FETCHING || _fstate == WANT_MANIFEST) return;
  if (resumeStaged(mid)) return;                 // resume a partial container left in flash
  memcpy(_fid, mid, 4);
  seedBlockRng();                                // per-node block-pick/jitter sequence (distinct per node)
  _fstate = WANT_MANIFEST;
  _mf_total = 0; _mf_mask = 0; _mf_len = 0; _mf_retries = 0;   // fresh manifest reassembly
  GetManifestMsg gm; memcpy(gm.manifest_id, _fid, 4);
  uint8_t b[16];
  emit(b, encode_get_manifest(b, sizeof(b), gm), false);
}

void OtaManager::handleManifest(const uint8_t* m, uint16_t n) {
  ManifestMsg mm;
  if (!decode_manifest(m, n, mm) || !_fetch) return;
  if (_fstate != WANT_MANIFEST || memcmp(mm.manifest_id, _fid, 4) != 0) return;
  if (mm.frag_total == 0 || mm.frag_total > OTA_MF_MAXFRAG || mm.frag_idx >= mm.frag_total) return;

  // reassemble the (possibly multi-fragment) manifest into _mf_buf; place fragment frag_idx at its offset
  uint32_t foff = (uint32_t)mm.frag_idx * OTA_MF_FRAG;
  if (foff + mm.len > sizeof(_mf_buf)) return;
  if (mm.frag_total != _mf_total) { _mf_total = mm.frag_total; _mf_mask = 0; _mf_len = 0; }  // (re)start
  memcpy(_mf_buf + foff, mm.bytes, mm.len);
  _mf_mask |= (uint16_t)(1u << mm.frag_idx);
  if (mm.frag_idx == mm.frag_total - 1) _mf_len = foff + mm.len;     // last fragment fixes the length
  uint16_t full = (mm.frag_total >= 16) ? 0xFFFF : (uint16_t)((1u << mm.frag_total) - 1);
  if (_mf_mask != full || _mf_len == 0) return;                     // wait until every fragment is in

  const uint8_t* mf = _mf_buf;                   // fully reassembled manifest-minus-leaves
  uint32_t mfl = _mf_len;
  if (mfl != MOTA_MFL) { _fstate = FAILED; return; }   // manifest-minus-leaves is a fixed 197 bytes
  if (!codecOk(mf[56])) { _fstate = IDLE; return; }   // codec we can't apply (lying/stale ADV) — abort
  uint32_t payload_size = rd_u32le(mf + 15);
  uint8_t  bsl = mf[19];
  uint32_t bs = 1u << bsl;
  // a block must fit our reassembly buffer (and be non-empty) — reject an oversized block_size up front
  if (bs == 0 || bs > OTA_MAX_BLOCK || payload_size == 0) { _fstate = FAILED; return; }
  uint32_t bc = (payload_size + bs - 1) / bs;
  if (bc > 0xFFFFu) { _fstate = FAILED; return; }   // block_idx is uint16 on the wire — can't address more
  memcpy(_froot, mf + 20, 4);

  uint32_t leaves_off = 8 + mfl;
  uint32_t payload_off = leaves_off + bc * 4;
  uint32_t total = payload_off + payload_size + 5;

  // Hand the store the parsed layout BEFORE begin(), so a partition-backed store (ESP32) can choose
  // placement and refuse an unfittable fetch up front: a FULL payload streams to the inactive slot,
  // a delta's whole container is staged together. (image_size at mf+11, is_full from flags at mf+1.)
  bool is_full = (mf[1] & MFLAG_FULL) != 0;
  if (!_fetch->plan_layout(is_full, rd_u32le(mf + 11), payload_off, payload_size)) { _fstate = FAILED; return; }
  unserveFetched();   // the store is about to be overwritten by this new fetch — stop re-seeding the old one
  if (!_fetch->begin(total)) { _fstate = FAILED; return; }
  // declare the metadata extent so a flash store can pin it (leaves are written all transfer long)
  if (!_fetch->set_meta_size(payload_off)) { _fstate = FAILED; return; }
  uint8_t hdr[8];
  memcpy(hdr, MOTA_MAGIC, 4);
  wr_u32le(hdr + 4, total);
  if (!_fetch->write(0, hdr, 8) ||
      !_fetch->write(8, mf, mfl) ||
      !_fetch->write(total - 5, MOTA_TRAILER, 5)) { _fstate = FAILED; return; }

  _fflags = mf[1];   // manifest flags (FULL/SIGNED) of the fetch in progress (auto-install gate)
  _fpoff = payload_off; _floff = leaves_off; _fpsize = payload_size; _fbc = bc; _fbs = bs;
  _ftotal = total; _have = 0; _fstate = FETCHING;
  clearReassembly();                             // fresh transfer: drop any prior per-block state
  _loop_last_have = 0; _loop_last_mask = 0;
  if (_rng == 0) seedBlockRng();
  armFirstReqHold();
  OTA_DBG("OTA: FETCHING bc=%u bs=%u total=%u\n", (unsigned)bc, (unsigned)bs, (unsigned)total);
}

bool OtaManager::resumeStaged(const uint8_t* want_mid) {
  if (!_fetch || _fstate == FETCHING || _fstate == WANT_MANIFEST) return false;
  if (!_fetch->reopen()) return false;                  // nothing persisted in the store
  uint32_t total = _fetch->staged_size();
  uint8_t hdr[8];
  if (total < 13 || !_fetch->read(0, hdr, 8) || memcmp(hdr, MOTA_MAGIC, 4) != 0) return false;
  // read + parse the stored manifest (everything before leaves[]) to recompute the geometry
  uint8_t mbuf[256];
  uint32_t mread = total - 8; if (mread > sizeof(mbuf)) mread = sizeof(mbuf);
  MotaManifest m;
  if (!_fetch->read(8, mbuf, mread) || !mota_parse_manifest(mbuf, mread, m)) return false;
  if (want_mid && memcmp(m.merkle_root, want_mid, 4) != 0) return false;   // a different fw is staged
  if (!codecOk(m.codec_id)) return false;
  uint32_t mfl = (uint32_t)(m.approval - m.manifest_start) + 4;            // manifest-minus-leaves length
  uint32_t bs = m.block_size();
  if (bs == 0 || bs > OTA_MAX_BLOCK) return false;
  uint32_t bc = m.block_count;
  uint32_t leaves_off = 8 + mfl;
  uint32_t payload_off = leaves_off + bc * 4;
  if ((uint64_t)payload_off + m.payload_size + 5 != total) return false;   // geometry must match the header

  memcpy(_fid, m.merkle_root, 4);
  memcpy(_froot, m.merkle_root, 4);
  _fflags = m.flags;
  _fpoff = payload_off; _floff = leaves_off; _fpsize = m.payload_size; _fbc = bc; _fbs = bs;
  _ftotal = total;
  _have = 0;
  for (uint32_t i = 0; i < bc; i++) if (blockPresent(i)) _have++;   // count blocks whose leaf survived
  clearReassembly();
  _loop_last_have = 0; _loop_last_mask = 0;
  OTA_DBG("OTA: RESUME have=%u/%u total=%u\n", (unsigned)_have, (unsigned)bc, (unsigned)total);

  if (_have >= bc) {                                  // already complete -> verify root + finalize
    if (bc * 4 <= sizeof(_scratch) && _fetch->read(_floff, _scratch, bc * 4)) {
      uint8_t root[4]; merkle_root(root, _scratch, bc);
      _fstate = (memcmp(root, _froot, 4) == 0) ? COMPLETE : FAILED;
    } else {
      _fstate = COMPLETE;
    }
    if (_fstate == COMPLETE) { _fetch->finalize(); serveFetched(); }
    return true;
  }
  _fstate = FETCHING;                                 // resume fetching the holes
  // De-sync the first REQ exactly like a fresh fetch, so a coordinated reboot (whole-site power-cycle)
  // doesn't make every resuming node REQ in lockstep.
  seedBlockRng();
  armFirstReqHold();
  _loop_last_have = _have; _loop_last_mask = _reasm_mask;   // "no progress yet" -> loop will request after the hold
  return true;
}

uint32_t OtaManager::blockLen(uint32_t i) const {
  uint32_t off = i * _fbs;
  return (off + _fbs <= _fpsize) ? _fbs : (_fpsize - off);
}

bool OtaManager::blockPresent(uint32_t i) const {
  uint8_t leaf[4];
  if (!_fetch->read(_floff + i * 4, leaf, 4)) return false;
  return !(leaf[0]==0xFF && leaf[1]==0xFF && leaf[2]==0xFF && leaf[3]==0xFF);
}

void OtaManager::handleData(const uint8_t* m, uint16_t n) {
  DataMsg dm;
  if (!decode_data(m, n, dm) || !_fetch) return;
  if (_fstate != FETCHING || memcmp(dm.manifest_id, _fid, 4) != 0) return;
  if (dm.block_idx >= _fbc) return;
  if (blockPresent(dm.block_idx)) return;                   // already stored + verified
  uint32_t blen = blockLen(dm.block_idx);
  if (dm.frag_off % OTA_FRAG_DATA != 0) return;             // canonical FRAG_DATA-aligned slices only
  if ((uint32_t)dm.frag_off + dm.data_len > blen) return;   // slice out of the block
  if (dm.block_idx != _reasm_block) {                       // (re)start reassembly for this block
    _reasm_block = dm.block_idx; _reasm_mask = 0; _awaiting_proof = false;
    uint32_t nf = (blen + OTA_FRAG_DATA - 1) / OTA_FRAG_DATA;
    _reasm_need = (nf >= 16) ? 0xFFFF : (uint16_t)((1u << nf) - 1);
  }
  uint32_t kf = dm.frag_off / OTA_FRAG_DATA;
  if (kf >= 16) return;
  memcpy(_reasm_buf + dm.frag_off, dm.data, dm.data_len);
  _reasm_mask |= (uint16_t)(1u << kf);
  if (_reasm_mask != _reasm_need || _awaiting_proof) return;  // wait for all slices (or proof already asked)
  // block fully reassembled -> request its proof (data + proof are fetched separately)
  _awaiting_proof = true;
  ReqProofMsg rp; memcpy(rp.manifest_id, _fid, 4); rp.block_idx = (uint16_t)_reasm_block;
  uint8_t b[16]; emit(b, encode_req_proof(b, sizeof(b), rp), false);
}

void OtaManager::handleProof(const uint8_t* m, uint16_t n) {
  ProofMsg pm;
  if (!decode_proof(m, n, pm) || !_fetch) return;
  if (_fstate != FETCHING || memcmp(pm.manifest_id, _fid, 4) != 0) return;
  if (!_awaiting_proof || pm.block_idx != _reasm_block) return;   // not the block we're verifying
  uint32_t blen = blockLen(_reasm_block);
  if (!merkle_verify(_reasm_buf, blen, _reasm_block, pm.proof, pm.n_proof, _froot, _fbc)) {
    clearReassembly();                                                      // bad -> drop, re-fetch the block
    return;
  }
  // verified -> commit the payload block, then its leaf (the present marker)
  if (!_fetch->write(_fpoff + (uint32_t)_reasm_block * _fbs, _reasm_buf, blen)) return;
  uint8_t leaf[4]; merkle_leaf(leaf, _reasm_buf, blen);
  if (!_fetch->write(_floff + (uint32_t)_reasm_block * 4, leaf, 4)) return;
  _have++;
  OTA_DBG("OTA: block %u OK  have=%u/%u\n", (unsigned)_reasm_block, (unsigned)_have, (unsigned)_fbc);
  clearReassembly();
  // periodically persist progress (meta/leaf page + open payload) so a reboot can resume (no-op for RAM);
  // cadence is runtime-tunable via `ota config checkpoint <N>` (0 = never)
  if (_checkpoint_blocks && _have % _checkpoint_blocks == 0) _fetch->checkpoint();
  if (_have < _fbc) { requestMissing(); return; }            // next block
  // all blocks present -> final root cross-check + finalize
  if (_fbc * 4 <= sizeof(_scratch) && _fetch->read(_floff, _scratch, _fbc * 4)) {
    uint8_t root[4]; merkle_root(root, _scratch, _fbc);
    _fstate = (memcmp(root, _froot, 4) == 0) ? COMPLETE : FAILED;
  } else {
    _fstate = COMPLETE;   // per-block proofs already guaranteed integrity vs the root
  }
  if (_fstate == COMPLETE) { _fetch->finalize(); serveFetched(); }   // commit + re-seed (epidemic spread)
  OTA_DBG("OTA: transfer %s\n", _fstate == COMPLETE ? "COMPLETE" : "FAILED(root)");
}

void OtaManager::requestMissing() {
  if (_fstate != FETCHING) return;
  // Per-block serial flow (split data/proof). If the current block's data is fully reassembled and we
  // are waiting on its proof, (re)send the proof request rather than re-fetching the data — this also
  // recovers from a lost PROOF reply.
  if (_awaiting_proof && _reasm_block != NO_BLOCK) {
    ReqProofMsg rp; memcpy(rp.manifest_id, _fid, 4); rp.block_idx = (uint16_t)_reasm_block;
    uint8_t b[16]; emit(b, encode_req_proof(b, sizeof(b), rp), false);
    OTA_DBG("OTA: REQ_PROOF block=%u (have=%u/%u)\n",
            (unsigned)_reasm_block, (unsigned)_have, (unsigned)_fbc);
    return;
  }
  // Otherwise request the DATA fragments of the next missing block. One block at a time keeps the
  // server's TX queue tiny so OTA never floods the mesh (docs/ota_protocol.md §8); a block's fragments
  // are self-describing (frag_off) so they may be served by ANY peer, BitTorrent-style.
  uint32_t start = pickMissingBlock();
  if (start >= _fbc) return;
  _req_start = start; _req_count = 1;
  ReqMsg rq; memcpy(rq.manifest_id, _fid, 4);
  rq.start_block = (uint16_t)start; rq.count = 1;
  uint8_t b[16];
  OTA_DBG("OTA: REQ block=%u (have=%u/%u mask=%04x)\n",
          (unsigned)start, (unsigned)_have, (unsigned)_fbc, (unsigned)_reasm_mask);
  emit(b, encode_req(b, sizeof(b), rq), false);
}

// Choose which block to request next. Swarm-aware so N fetchers of the same mid spread their load instead
// of marching in lockstep on the same block:
//  - finish an in-flight partially-reassembled block first (don't waste received fragments);
//  - otherwise pick a RANDOM missing block (de-correlates fetchers -> they collectively pull different
//    blocks, and every broadcast DATA fills everyone's hole);
//  - skip a block a peer just REQ'd (its DATA is already coming over the air) unless it's all that's left.
// Returns _fbc if nothing to request.
uint32_t OtaManager::pickMissingBlock() {
  if (_fbc == 0) return _fbc;
  // (1) keep finishing a block we've already started reassembling (recover its lost fragments)
  if (_reasm_block < _fbc && !blockPresent(_reasm_block) && _reasm_mask != 0) return _reasm_block;
  // (2) count missing blocks
  uint32_t miss = 0;
  for (uint32_t i = 0; i < _fbc; i++) if (!blockPresent(i)) miss++;
  if (miss == 0) return _fbc;
  bool suppress = (_peer_req_block < _fbc) && ((uint32_t)(_now_ms - _peer_req_at) < OTA_REQ_SUPPRESS_MS);
  // (3) pick the k-th missing block (k from the per-node RNG), optionally skipping the peer-REQ'd one
  uint32_t k = rngNext() % miss;
  uint32_t seen = 0, chosen = _fbc, firstAny = _fbc;
  for (uint32_t i = 0; i < _fbc; i++) {
    if (blockPresent(i)) continue;
    if (firstAny == _fbc) firstAny = i;
    if (seen == k) { chosen = i; }
    seen++;
  }
  if (suppress && chosen == _peer_req_block) {          // pick a different missing block than the one in flight elsewhere
    for (uint32_t i = 0; i < _fbc; i++) {
      uint32_t j = (chosen + 1 + i) % _fbc;
      if (!blockPresent(j) && j != _peer_req_block) { chosen = j; break; }
    }
    // if the suppressed block is the ONLY one left, chosen stays == it (we still need it eventually)
  }
  return (chosen < _fbc) ? chosen : firstAny;
}

// Observe a peer's OTA_REQ for the mid we're fetching: its block's DATA is broadcast, so it will fill our
// hole too — note it so pickMissingBlock() spends our next REQ on a DIFFERENT block (swarm de-dup).
void OtaManager::noteOverheardReq(const uint8_t* m, uint16_t n) {
  if (_fstate != FETCHING) return;
  ReqMsg rq;
  if (!decode_req(m, n, rq) || memcmp(rq.manifest_id, _fid, 4) != 0) return;
  _peer_req_block = rq.start_block;
  _peer_req_at = _now_ms;
}

void OtaManager::loop() {
  // fire a scheduled catalog query once its jitter has elapsed (unless overhearing already suppressed it)
  if (_pq_active && (int32_t)(_now_ms - _pq_at) >= 0) {
    _pq_active = false;
    sendQuery(_pq_seeder, _pq_digest, 0);    // unfiltered: one broadcast HAVE serves everyone
  }
  if (_fstate == WANT_MANIFEST) {
    // the MANIFEST reply may have been lost on a marginal link — retry GET_MANIFEST, but give up after a
    // cap so an unreachable mid doesn't pin the single fetch slot (or emit) forever.
    if (++_mf_retries > OTA_MANIFEST_MAX_RETRY) { _fstate = FAILED; return; }
    GetManifestMsg gm; memcpy(gm.manifest_id, _fid, 4);
    uint8_t b[16];
    emit(b, encode_get_manifest(b, sizeof(b), gm), false);
    return;
  }
  if (_fstate != FETCHING) return;
  if ((int32_t)(_now_ms - _req_hold_at) < 0) return;   // swarm: initial random hold (de-sync N fetchers)
  // retry only when a whole tick passed with NO progress — neither a committed block nor a new fragment
  // of the in-flight block. This avoids re-request spam while a block's fragments are still streaming in.
  if (_have == _loop_last_have && _reasm_mask == _loop_last_mask) requestMissing();
  _loop_last_have = _have;
  _loop_last_mask = _reasm_mask;
}

// ---------------- dispatch ----------------

void OtaManager::on_message(const uint8_t* msg, uint16_t len) {
  switch (ota_msg_type(msg, len)) {
    case OTA_ADV:          handleAdv(msg, len); break;
    case OTA_QUERY:        handleQuery(msg, len); break;
    case OTA_HAVE:         handleHave(msg, len); break;
    case OTA_GET_MANIFEST: handleGetManifest(msg, len); break;
    case OTA_MANIFEST:     handleManifest(msg, len); break;
    case OTA_REQ:          noteOverheardReq(msg, len); handleReq(msg, len); break;
    case OTA_DATA:         handleData(msg, len); noteOverheardData(msg, len); break;
    case OTA_REQ_PROOF:    handleReqProof(msg, len); break;
    case OTA_PROOF:        handleProof(msg, len); break;
    default: break;
  }
}

} // namespace ota
} // namespace mesh
