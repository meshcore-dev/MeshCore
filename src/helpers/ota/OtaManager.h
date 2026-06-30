#pragma once

#include <stdint.h>
#include <stddef.h>
#include "OtaFormat.h"
#include "OtaByteIO.h"
#include "OtaStore.h"
#include "MotaContainer.h"
#include "OtaSource.h"

// Transport-agnostic OTA session engine (docs/ota_protocol.md §5/§8). It SERVES a complete `.mota`
// (answering GET_MANIFEST / REQ) and/or FETCHES one into an OtaStore (verifying every block against
// the signed merkle root via proofs). It is portable (no Arduino / radio / Ed25519) so it can be
// driven by a host simulation; a thin Mesh adapter wires it to PAYLOAD_TYPE_OTA on device.
//
// Transfer is per-block and 2-phase: a 1 KB logical block is fetched as self-describing DATA fragments
// (frag_off, so any peer can serve any fragment — BitTorrent-style), reassembled, then its merkle PROOF
// is requested separately and verified against the signed root before the block is committed.

namespace mesh {
namespace ota {

// Emit an OTA message (one packet payload). `flood`=true for announce/query, false for direct replies.
typedef void (*OtaSend)(void* ctx, const uint8_t* msg, uint16_t len, bool flood);

// Read `len` payload bytes at offset `off` from the serve source (flash-backed self-serve); false on
// error. nullptr means the payload is a contiguous RAM buffer (the staged `.mota`).
typedef bool (*ServeReadFn)(void* ctx, uint32_t off, uint8_t* buf, uint32_t len);

#ifndef OTA_PROOFGEN_SCRATCH
#define OTA_PROOFGEN_SCRATCH 4096   // server proof-gen working buffer (supports up to 1024 blocks)
#endif

#ifndef OTA_MAX_BLOCK
#define OTA_MAX_BLOCK 1024          // largest logical block (merkle leaf unit) = reassembly buffer size
#endif
#ifndef OTA_CHECKPOINT_BLOCKS
#define OTA_CHECKPOINT_BLOCKS 4     // persist progress (store.checkpoint) every N committed blocks (resume)
#endif
#ifndef OTA_ADVERT_INTERVAL_MINS
#define OTA_ADVERT_INTERVAL_MINS 1440   // re-advertise our served set every N minutes after the boot burst (24h)
#endif
#ifndef OTA_MF_FRAG
#define OTA_MF_FRAG 176             // manifest bytes per OTA_MANIFEST fragment (<= MAX_PACKET_PAYLOAD - header)
#endif
#ifndef OTA_MF_MAXFRAG
#define OTA_MF_MAXFRAG 4            // max manifest fragments (the fixed 197 B manifest is always 2)
#endif
#ifndef OTA_MANIFEST_MAX_RETRY
#define OTA_MANIFEST_MAX_RETRY 20   // give up (FAILED) after this many GET_MANIFEST retries — frees the slot
#endif
#ifndef OTA_MAX_SOURCES
#define OTA_MAX_SOURCES 12          // heard OTA sources (beacon senders) tracked (LRU); ~12 B each
#endif
#ifndef OTA_MAX_SERVE
#define OTA_MAX_SERVE 12            // mOTAs THIS node offers (own fw + external folder); == one HAVE fragment
#endif
#ifndef OTA_MAX_SOURCE_OBJ
#define OTA_MAX_SOURCE_OBJ 4        // external MotaSource objects (folders/transports) attached at once
#endif
#ifndef OTA_SRC_MANIFEST_MAX
#define OTA_SRC_MANIFEST_MAX 256    // manifest-minus-leaves buffer for the loaded source mota (head+sig+approval)
#endif
#ifndef OTA_MAX_CATALOG
#define OTA_MAX_CATALOG 12          // distinct mOTAs catalogued from OTA_HAVE replies (LRU)
#endif
#ifndef OTA_QUERY_MIN_MS
#define OTA_QUERY_MIN_MS 300        // min delay before sending a catalog query (overhear-suppression window)
#endif
#ifndef OTA_QUERY_SPREAD_MS
#define OTA_QUERY_SPREAD_MS 4000    // random jitter span so 50 neighbours don't all query at once (storm)
#endif
#ifndef OTA_REQ_SPREAD_MS
#define OTA_REQ_SPREAD_MS 3000      // initial random hold before a fetch's first REQ (de-sync N fetchers)
#endif
#ifndef OTA_REQ_SUPPRESS_MS
#define OTA_REQ_SUPPRESS_MS 2500    // after overhearing a peer's REQ for a block, don't also request it —
#endif                              // its DATA is broadcast and will fill our hole too (swarm de-dup)
#ifndef OTA_SERVE_SUPPRESS_MS
#define OTA_SERVE_SUPPRESS_MS 1500  // don't re-serve a block whose DATA we just overheard another holder send
#endif                              // (so multiple sources of the same mota don't duplicate-broadcast it)
#ifndef OTA_FRAG_DATA
#define OTA_FRAG_DATA 160           // data bytes per DATA fragment (<= MAX_PACKET_PAYLOAD - 9-byte header)
#endif
// nRF52 note: a flash page-erase halts the CPU (~85 ms, code runs from flash) and starves the LoRa RX,
// so writing to flash on every received packet drops in-flight DATA and the transfer stalls. The SD-safe
// driver (Adafruit flash_nrf5x) always erases on flush, so there is no erase-free write; instead
// OtaStoreFlashNrf52 COALESCES to the 4 KB page (the erase unit) and writes each page once — RAM stays
// O(one page), never O(mota). It pins flash page 0 (header+manifest+merkle leaves, which update all
// transfer long) in RAM and streams the payload through one sliding page buffer, flushing page 0 and the
// last page at finalize() (radio idle). Flash is then touched ~once per 4 KB (≈1 per 4 blocks), not per
// packet; a small delta that fits page 0 does ZERO flash I/O until COMPLETE. (Pacing alone is not enough.)

class OtaManager {
public:
  enum FetchState : uint8_t { IDLE, WANT_MANIFEST, FETCHING, COMPLETE, FAILED };

  // Sentinel for "no block" in the reassembly / peer-REQ / recently-served slots (a real block index is
  // a small uint16, so 0xFFFFFFFF is never valid).
  static const uint32_t NO_BLOCK = 0xFFFFFFFFu;

  // --- multi-mota serve ---  A ServeView is everything a serve handler needs for ONE mota. Two can be
  // resident: view0 = our own fw / a RAM `.mota` (always loaded), plus one on-demand source view that is
  // (re)loaded from a MotaSource when a request targets a different external mota. Requests dispatch by
  // manifest_id (carried in every fetch message) -> the matching ServeView via resolve().
  struct ServeView {
    bool          valid = false;
    MotaManifest  m;                       // parsed manifest (fields/pointers into the backing buffers)
    uint16_t      mfl = 0;                 // manifest-minus-leaves length (the OTA_MANIFEST payload)
    ServeReadFn   read = nullptr;          // payload reader (nullptr => m.payload is contiguous in RAM)
    void*         read_ctx = nullptr;
    uint8_t*      scratch = nullptr;       // proof-gen working buffer (>= block_count*4)
    uint32_t      scratch_sz = 0;
  };
  // A lightweight catalog entry: what we advertise per mota + how to load its ServeView on demand.
  struct ServeEntry {
    uint8_t     mid[4];
    uint32_t    target_id, fw_version;
    uint8_t     codec_id, flags;
    uint32_t    have_count;                // blocks we currently hold (== block_count when complete)
    bool        is_self;                   // true => entry is view0 (our own fw / RAM mota)
    bool        is_fetch;                  // true => load from our own fetch store (a completed download we re-seed)
    MotaSource* src;                       // else: load from this external source ...
    uint8_t     src_idx;                   // ... at this index
    MotaDesc    desc;                      // cached region offsets (source / fetch entries)
  };
  // Context for the source-payload reader trampoline (maps a payload-relative offset to a backing read:
  // an external MotaSource, or — when `store` is set — our own fetch store, for re-seeding a completed mota).
  struct SrcReadCtx { MotaSource* src; uint8_t idx; uint32_t payload_off; OtaStore* store; };

  void begin(uint32_t my_target_id, OtaSend send, void* ctx);

  // --- serve ---  Provide a complete, contiguous `.mota` to serve (caller keeps it alive).
  bool serve(const uint8_t* mota, uint32_t len);
  // Serve from a non-contiguous source (e.g. our own firmware in flash): a pre-assembled manifest
  // (manifest-minus-leaves, `mfl` bytes), the pre-computed merkle `leaves` (kept alive by caller), and a
  // `read` callback for payload blocks. Lets a node host its own image without holding it in RAM.
  bool serve_self(const uint8_t* manifest, uint16_t mfl, const uint8_t* leaves, uint32_t block_count,
                  uint8_t* proof_scratch, uint32_t proof_scratch_sz, ServeReadFn read, void* ctx);
  // Attach an external "folder" of `.mota` images (USB-serial daemon, BLE, WiFi URLs, NFS/samba, ...).
  // The node then advertises + RELAYS them transparently alongside its own fw — peers just see more mOTAs.
  // Re-enumerates the source into the serve registry. Returns false if no slots remain. (Trustless: the
  // fetcher verifies merkle+signature, so the source is never trusted — see OtaSource.h.)
  bool add_source(MotaSource* src);
  // Re-read every attached source's catalog (call when the folder's contents change). Rebuilds entries
  // [1..] from the sources; entry 0 (our own fw) is preserved.
  void refresh_sources();
  // Drop all external sources (keep serving our own fw).
  void clear_sources();
  uint8_t servedCount() const { return _n_serve; }   // total mOTAs we offer (own fw + folder)
  // Read-only view of one served entry (for `ota serve` listing): mid/target/fwver/codec/flags + is_self.
  const ServeEntry* servedEntry(uint8_t i) const { return i < _n_serve ? &_serve[i] : nullptr; }

  // Broadcast the tiny per-node BEACON (OTA_ADV): seeder_id + count + set-digest of everything we serve.
  // Constant size regardless of how many mOTAs — peers ask for the catalog via OTA_QUERY only on interest.
  void announce();

  // --- fetch ---  Provide the staging store; fetching starts on a matching OTA_ADV.
  void set_fetch_store(OtaStore* s) { _fetch = s; }

  // Resume a fetch from a container already persisted in the store (after a reboot). want_mid=nullptr
  // accepts whatever is staged; otherwise only resumes if the staged manifest_id matches. Re-parses the
  // stored manifest, recomputes geometry, counts present blocks, and continues FETCHING the holes (or goes
  // straight to COMPLETE if all blocks are present). Returns true if it adopted a staged container.
  bool resumeStaged(const uint8_t* want_mid);

  // Manual cross-target override (decision: deliberate role switch, e.g. companion -> repeater on the
  // same hardware). Normally a node only auto-fetches its OWN target_id; `want(T)` makes it accept an
  // ADV for target T instead (T=0 restores auto). The user takes responsibility for HW compatibility;
  // a hw_id brick-safety check is the planned safety layer (see docs/ota_protocol.md / plan).
  void want(uint32_t target_id) { _desired_target = target_id; reDiscover(); }
  uint32_t wanted() const { return _desired_target; }
  uint32_t target() const { return _target; }   // this node's own OTA target_id (set in begin)

  // Pull a SPECIFIC advertised mOTA by manifest_id (e.g. `ota pull <#>` picks the one more peers have),
  // not just any firmware for the target. mid=nullptr clears the filter (accept any mid for the target).
  void want_mid(const uint8_t* mid) {
    if (mid) { for (int i = 0; i < 4; i++) _desired_mid[i] = mid[i]; _have_desired_mid = true; }
    else _have_desired_mid = false;
    reDiscover();
  }

  // Begin fetching a chosen mid now (sets want + starts the manifest fetch / resume). Used by `ota pull`
  // once the user picks a catalogued mOTA (the source is reached via the flooded GET_MANIFEST).
  void pull(const uint8_t* mid, uint32_t target) { want(target); want_mid(mid); startFetch(mid, target); }
  // Ask every known source for its catalog (populates `ota neighbors`). Async — rows arrive via OTA_HAVE.
  void queryAll();
  // Coarse clock for source/catalog ages + LRU (the Mesh adapter feeds millis; 0 in host tests is fine).
  void set_clock(uint32_t ms) { _now_ms = ms; }

  // Codec compatibility: a node only fetches/accepts fw it can actually apply. CODEC_FULL is always
  // acceptable; the platform's single delta codec is set here (ESP32 A/B -> sequential, nRF52 single-
  // slot -> in-place). A mismatching `.mota` is rejected at OTA_ADV time, before fetching anything.
  void set_apply_codec(uint8_t c) { _apply_codec = c; }
  // A platform may apply MORE than one delta codec (ESP32 does both sequential AND in-place, so a single
  // in-place `.mota` can target both ESP32 and nRF52). 0xFF = unset.
  void set_apply_codec2(uint8_t c) { _apply_codec2 = c; }
  bool codecOk(uint8_t c) const { return c == CODEC_FULL || c == _apply_codec || c == _apply_codec2; }

  // Auto-fetch policy (manual `ota pull` always works regardless): 0=off (discover only), 1=any
  // compatible own-target advert, 2=only signed adverts. Conservative default = off.
  static const uint8_t AUTOFETCH_OFF = 0, AUTOFETCH_ANY = 1, AUTOFETCH_SIGNED = 2;
  void set_autofetch(uint8_t p) { _autofetch = p; reDiscover(); }
  uint8_t autofetch() const { return _autofetch; }

  // Resume checkpoint cadence (runtime-tunable, persisted in NodePrefs): persist progress every N
  // committed blocks. 0 = never (resume only from a finalized container). Default OTA_CHECKPOINT_BLOCKS.
  void set_checkpoint_blocks(uint16_t n) { _checkpoint_blocks = n; }
  uint16_t checkpoint_blocks() const { return _checkpoint_blocks; }

  // Beacon re-advertise cadence in MINUTES (runtime-tunable, persisted in NodePrefs): after the boot burst,
  // re-send the discovery beacon every N minutes so a long-running node stays discoverable. 0 = disabled
  // (boot burst only). Default OTA_ADVERT_INTERVAL_MINS (24h).
  void set_advert_mins(uint16_t m) { _advert_mins = m; }
  uint16_t advert_mins() const { return _advert_mins; }
  bool fetched_is_signed() const { return (_fflags & MFLAG_SIGNED) != 0; }  // flags of the fetched manifest

  // This node's id (pubkey[0:4]), stamped into adverts we send so receivers can count distinct seeders.
  void set_seeder_id(const uint8_t* id4) { if (id4) for (int i = 0; i < 4; i++) _seeder_id[i] = id4[i]; }

  void on_message(const uint8_t* msg, uint16_t len);   // feed one received OTA message
  void loop();                                         // drive fetch (re-request missing blocks)

  // Drop the current fetch session back to IDLE (so a fresh `ota pull` / advert starts a new one). Also
  // stops re-seeding a previously-completed download — callers clear the staging store right after, so the
  // re-seed view would otherwise advertise a mota we can no longer serve.
  void reset_session() {
    _fstate = IDLE; _have = 0; _req_count = 0; _mf_retries = 0;
    clearReassembly();
    _loop_last_have = 0; _loop_last_mask = 0;
    _mf_total = 0; _mf_mask = 0; _mf_len = 0;
    unserveFetched();
  }

  FetchState fetchState() const { return _fstate; }
  uint32_t blocksHave() const { return _have; }
  uint32_t blocksTotal() const { return _fbc; }
  const uint8_t* fetchManifestId() const { return _fid; }

  // --- discovery catalog (for `ota neighbors`): mOTAs heard around us via OTA_HAVE, deduped by mid ---
  static const uint8_t OTA_CAT_SEEDERS = 4;   // distinct sources tracked per catalog row (for "N nodes have it")
  struct CatRow {
    uint8_t  mid[4];
    uint32_t target_id, fw_version;
    uint8_t  codec, flags;
    uint8_t  seeders[OTA_CAT_SEEDERS][4];  // distinct sources advertising this mid (deduped; capped)
    uint8_t  n_seeders;                    // count of the above (capped at OTA_CAT_SEEDERS) — "N+ nodes have it"
    uint32_t have_max;                     // best block-count any source reported (== total when a full copy exists)
    uint32_t last_ms;
  };
  uint8_t catalogCount() const { return _n_cat; }
  const CatRow* catalogRow(uint8_t i) const { return i < _n_cat ? &_catalog[i] : nullptr; }
  uint8_t sourceCount() const { return _n_src; }   // distinct OTA sources (beacon senders) heard

private:
  void emit(const uint8_t* b, uint16_t n, bool flood) { if (_send && n) _send(_ctx, b, n, flood); }
  void handleAdv(const uint8_t* m, uint16_t n);     // beacon -> sources table (+ query if interested)
  void handleQuery(const uint8_t* m, uint16_t n);   // serve: reply OTA_HAVE catalog
  void handleHave(const uint8_t* m, uint16_t n);    // peer: catalog rows (+ startFetch if a row matches)
  void handleGetManifest(const uint8_t* m, uint16_t n);
  void handleManifest(const uint8_t* m, uint16_t n);
  void handleReq(const uint8_t* m, uint16_t n);
  void handleData(const uint8_t* m, uint16_t n);
  void handleReqProof(const uint8_t* m, uint16_t n);
  void handleProof(const uint8_t* m, uint16_t n);
  void startFetch(const uint8_t* mid, uint32_t target);   // begin/resume a fetch of a chosen mid
  bool wantRow(const uint8_t* mid, uint32_t target, uint8_t codec, uint8_t flags) const;  // fetch this row?
  void noteOverheardReq(const uint8_t* m, uint16_t n);    // observe a peer's OTA_REQ (swarm de-dup)
  uint32_t rngNext() { _rng = _rng * 1664525u + 1013904223u; return _rng; }  // per-node LCG (block pick/jitter)
  void seedBlockRng() { _rng = (rd_u32le(_seeder_id) ^ rd_u32le(_fid)) | 1u; }   // distinct per node (id^mid)
  void clearReassembly();                                 // forget the in-flight block (reset to NO_BLOCK)
  void armFirstReqHold();                                 // de-sync the first REQ across swarm peers (jitter)
  uint32_t pickMissingBlock();                            // choose the next block to request (swarm-aware)
  int  serveEntryIndex(const uint8_t* mid) const;         // registry slot serving this mid (-1 if none)
  ServeView* resolve(const uint8_t* mid);                 // pick/load the ServeView for this mid (nullptr)
  bool loadSource(const ServeEntry& e);                   // load an external mota into _srcv (head+leaves)
  void registerSelfEntry();                               // (re)build entry[0] from view0
  static bool srcReadTramp(void* c, uint32_t off, uint8_t* buf, uint32_t len);  // source payload reader
  void serveFetched();                                    // after COMPLETE: re-seed the staged mota (epidemic)
  void unserveFetched();                                  // stop re-seeding (store about to be overwritten)
  void emitBlockData(const uint8_t* mid, uint32_t idx, const uint8_t* data, uint32_t blen);  // DATA fragments
  bool recentlyServed(uint32_t blk) const;                // a peer just broadcast this block's DATA?
  void noteOverheardData(const uint8_t* m, uint16_t n);   // remember overheard DATA (serve de-dup)
  void sendQuery(const uint8_t* seeder, const uint8_t* digest, uint32_t filter_target);  // ask a source for its catalog
  void scheduleQuery(const uint8_t* seeder, const uint8_t* digest);   // jittered + suppressible
  void reDiscover() { for (uint8_t i = 0; i < _n_src; i++) _sources[i].have_catalog = false; _pq_active = false; }
  void setDigest(uint8_t out[4]) const;                   // sha2-256:4 over our served mids
  bool blockPresent(uint32_t i) const;
  void requestMissing();
  uint32_t blockLen(uint32_t i) const;

  uint32_t _target = 0;
  OtaSend  _send = nullptr;
  void*    _ctx = nullptr;

  // serve (multi-mota): view0 = our own fw / a RAM `.mota`; _srcv = the currently-loaded external mota.
  ServeView   _view0;
  ServeView   _srcv;
  uint8_t     _srcv_mid[4] = {0};
  SrcReadCtx  _srcv_rdctx = {nullptr, 0, 0};
  ServeEntry  _serve[OTA_MAX_SERVE];                 // catalog (what we advertise) — entry 0 is view0
  uint8_t     _n_serve = 0;
  MotaSource* _src_list[OTA_MAX_SOURCE_OBJ] = {nullptr};
  uint8_t     _n_src_obj = 0;
  bool        _fetch_served = false;                 // we re-seed our last completed download (epidemic spread)
  MotaDesc    _fetch_desc;                            // its catalog descriptor (mid + region offsets)
  uint8_t     _src_manifest[OTA_SRC_MANIFEST_MAX];   // manifest-minus-leaves of the loaded source mota
  uint8_t     _src_leaves[OTA_PROOFGEN_SCRATCH];     // leaves[] of the loaded source mota (<=1024 blocks)
  uint8_t     _scratch[OTA_PROOFGEN_SCRATCH];        // proof-gen / fetch root-check working buffer

  // fetch
  OtaStore*  _fetch = nullptr;
  FetchState _fstate = IDLE;
  uint8_t    _fid[4] = {0};
  uint8_t    _froot[4] = {0};
  uint32_t   _ftotal = 0, _fpoff = 0, _floff = 0, _fpsize = 0, _fbc = 0, _fbs = 0;
  uint32_t   _have = 0;
  uint32_t   _req_start = 0, _req_count = 0;   // last block requested (per-block serial flow; telemetry)
  uint32_t   _loop_last_have = 0;              // for stall detection in loop()
  // swarm load-spreading (so 50 fetchers don't all hammer the seeder for the same block in lockstep)
  uint32_t   _rng = 0;                         // per-node LCG state (seeded from seeder_id^fid)
  uint32_t   _req_hold_at = 0;                 // _now_ms before which we hold the first REQ (startup jitter)
  uint32_t   _peer_req_block = NO_BLOCK;       // a block a peer just REQ'd (its broadcast DATA will fill us)
  uint32_t   _peer_req_at = 0;                 // when we overheard it (suppression window)
  // serve-side de-dup: blocks whose DATA we recently overheard ANOTHER holder broadcast (don't re-serve)
  uint32_t   _recent_blk[8];
  uint32_t   _recent_at[8] = {0};
  uint8_t    _recent_i = 0;
  uint32_t   _desired_target = 0;              // manual cross-target override (0 = auto / own target)
  uint8_t    _desired_mid[4] = {0,0,0,0};      // pull a specific manifest_id (see want_mid)
  bool       _have_desired_mid = false;
  uint8_t    _apply_codec = CODEC_DETOOLS_SEQUENTIAL;  // platform's delta codec (OtaContext sets it)
  uint8_t    _apply_codec2 = 0xFF;                     // optional 2nd accepted delta codec (ESP32: in-place)
  uint8_t    _seeder_id[4] = {0,0,0,0};        // our node id (pubkey[0:4]) for advert seeder counting
  uint8_t    _autofetch = AUTOFETCH_OFF;       // auto-fetch policy (persisted in NodePrefs)
  uint16_t   _checkpoint_blocks = OTA_CHECKPOINT_BLOCKS;  // resume checkpoint cadence (persisted)
  uint16_t   _advert_mins = OTA_ADVERT_INTERVAL_MINS;    // beacon re-advertise cadence, minutes; 0=off (persisted)
  uint8_t    _fflags = 0;                       // flags of the manifest currently being fetched
  // multi-fragment reassembly of the current block (per-block 2-phase: fetch data, then its proof)
  uint32_t   _reasm_block = NO_BLOCK;          // block being reassembled / awaiting proof (none)
  uint16_t   _reasm_mask = 0;                  // received FRAG_DATA-slice bitmap (bit k = slice @ k*FRAG_DATA)
  uint16_t   _reasm_need = 0;                  // full mask once all slices of the current block are in
  bool       _awaiting_proof = false;          // data complete; REQ_PROOF sent, verify on PROOF
  uint16_t   _loop_last_mask = 0;              // fragment-level stall detection in loop()
  uint8_t    _reasm_buf[OTA_MAX_BLOCK];
  // multi-fragment manifest reassembly (a signed v2 manifest exceeds one packet)
  uint8_t    _mf_buf[OTA_MF_MAXFRAG * OTA_MF_FRAG];   // sized to the fragment cap so no valid manifest is silently dropped
  uint16_t   _mf_retries = 0;                          // GET_MANIFEST retries while WANT_MANIFEST (give up after a cap)
  uint8_t    _mf_total = 0;                    // frag_total of the manifest being reassembled (0 = none)
  uint16_t   _mf_mask = 0;                     // received manifest-fragment bitmap
  uint32_t   _mf_len = 0;                      // assembled manifest length (set by the last fragment)

  // discovery: heard sources (beacon senders) + the catalog assembled from their OTA_HAVE replies
  struct Source { uint8_t seeder[4]; uint8_t digest[4]; uint8_t n_motas; uint32_t last_ms; bool have_catalog; };
  Source     _sources[OTA_MAX_SOURCES];
  uint8_t    _n_src = 0;
  CatRow     _catalog[OTA_MAX_CATALOG];
  uint8_t    _n_cat = 0;
  uint32_t   _now_ms = 0;                       // coarse clock (fed by set_clock; for ages/LRU/jitter)
  // pending catalog query (jittered + suppressed on overhearing a matching QUERY/HAVE — anti-storm)
  bool       _pq_active = false;
  uint8_t    _pq_seeder[4] = {0};
  uint8_t    _pq_digest[4] = {0};
  uint32_t   _pq_at = 0;                         // _now_ms deadline to actually send the query
};

} // namespace ota
} // namespace mesh
