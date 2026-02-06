#include "MyMesh.h"
#include <algorithm>

/* ------------------------------ Config -------------------------------- */

#ifndef LORA_FREQ
  #define LORA_FREQ 869.618
#endif
#ifndef LORA_BW
  #define LORA_BW 62.5
#endif
#ifndef LORA_SF
  #define LORA_SF 8
#endif
#ifndef LORA_CR
  #define LORA_CR 8
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

#ifndef ADVERT_NAME
  #define ADVERT_NAME "repeater"
#endif
#ifndef ADVERT_LAT
  #define ADVERT_LAT 0.0
#endif
#ifndef ADVERT_LON
  #define ADVERT_LON 0.0
#endif

#ifndef ADMIN_PASSWORD
  #define ADMIN_PASSWORD "password"
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY 300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY 200
#endif

#define FIRMWARE_VER_LEVEL       2

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_NEIGHBOURS     0x06
#define REQ_TYPE_GET_OWNER_INFO     0x07     // FIRMWARE_VER_LEVEL >= 2

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define ANON_REQ_TYPE_REGIONS      0x01
#define ANON_REQ_TYPE_OWNER        0x02
#define ANON_REQ_TYPE_BASIC        0x03   // just remote clock

#define CLI_REPLY_DELAY_MILLIS      600

#define LAZY_CONTACTS_WRITE_DELAY    5000

// Fixed RSSI margin (dB above the noise floor) at which a DIRECT resend treats the channel as busy
// (isResendChannelActive(): busy when isReceivingPacket() || currentRSSI - noiseFloor >= this). It is
// the resend-only counterpart of the general-LBT _prefs.interference_threshold, which flows through
// getInterferenceThreshold() -> triggerNoiseFloorCalibrate() -> RadioLibWrapper::isChannelActive().
// They are separate today only because interference_threshold is blanket-initialised to 0 (disabled;
// see getInterferenceThreshold(), cited there as a currentRSSI() reliability concern). The margin
// currentRSSI-noiseFloor is only as trustworthy as both terms: the median noise-floor estimator
// (PR #2933) stabilises getNoiseFloor() and removes the dominant drift source (the old one-way
// ratchet to -120 that inflated every margin), substantially rehabilitating the threshold path.
// Once interference_threshold is enabled across the fleet, this constant can be superseded by it.
#ifndef RESEND_INTERFERENCE_MARGIN
  #define RESEND_INTERFERENCE_MARGIN  12   // dB above noise floor that blocks a resend (non-invasive LBT)
#endif

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
#if MAX_NEIGHBOURS // check if neighbours enabled
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  bool is_refresh = neighbour->id.matches(id);   // same neighbour already in this slot? (computed before the id overwrite below)
  // Part 3: a NEW identity in this slot (empty slot, or LRU eviction of a *different* neighbour)
  // must start with unknown M-reachability. A refresh of the SAME neighbour (the common case --
  // putNeighbour runs on every ~2-min advert) keeps its reachability state intact.
  if (!is_refresh) {
    neighbour->m_reach_confirmed = false;
    neighbour->m_reach_timeouts = 0;
    neighbour->m_reach_last_ok_ms = 0;
  }
  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  // Smooth the link-quality estimate on a refresh of a KNOWN neighbour (the common ~2-min advert
  // path), so a single weak/strong advert does not jump the value the adaptive p75 / near test read.
  // A NEW slot (different id) is seeded from the single advert sample. EMA α≈0.25 (x4), matching
  // touchNeighbourByHash -- without this the advert hard-replace would reset that smoothing ~2 min.
  int8_t adv = (int8_t)(snr * 4);
  neighbour->snr = is_refresh ? (3 * neighbour->snr + adv) / 4 : adv;
#endif
}

// Refresh a *known* neighbour's liveness from an overheard forward, without waiting
// for its (rare) advert. A forwarded FLOOD carries only the forwarders' path
// *hashes*, not full identities, so this can only update an entry already seeded by
// an advert / node-discovery (putNeighbour) -- it cannot create a new one (empty
// slots have no identity to match, hence the heard_timestamp == 0 skip). The LAST
// path hash is the most recent forwarder, i.e. our immediate RF neighbour.
// SNR is stored as a running mean (x4 fixed-point, same scale as NeighbourInfo::snr)
// so a single outlier copy does not skew the link-quality estimate used by
// updateAdaptiveFloodParams().
void MyMesh::touchNeighbourByHash(const mesh::Packet* packet) {
#if MAX_NEIGHBOURS
  uint8_t count = packet->getPathHashCount();
  if (count < 1) return;                       // no forwarder hash -> immediate sender not identifiable
  uint8_t hs = packet->getPathHashSize();
  const uint8_t* last = packet->path + (count - 1) * hs;   // most recent forwarder == our RF neighbour
  int8_t new_snr = (int8_t)(packet->getSNR() * 4);          // x4, same scale as NeighbourInfo::snr
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (neighbours[i].heard_timestamp == 0) continue;       // empty slot: no identity to match (cannot seed here)
    if (neighbours[i].id.isHashMatch(last, hs)) {
      neighbours[i].heard_timestamp = getRTCClock()->getCurrentTime();
      neighbours[i].snr = (3 * neighbours[i].snr + new_snr) / 4;  // EMA α≈0.25 (x4): new sample 25%, outlier shifts ≤3 dB not halfway
      return;   // at most one slot matches a given hash
    }
  }
#endif
}

// Is neighbours[i] a "near" coverage peer? fresh (<= NEIGHBOUR_FRESH_S) and link
// SNR >= flood_suppress_snr_lo. Distant/weak neighbours are edge nodes, excluded
// (same intent as the old SNR-weighting weight-0).
bool MyMesh::isNearNeighbour(int i, uint32_t now) const {
#if MAX_NEIGHBOURS
  if (neighbours[i].heard_timestamp == 0) return false;                              // empty slot
  if ((uint32_t)(now - neighbours[i].heard_timestamp) > NEIGHBOUR_FRESH_S) return false;  // stale
  int8_t lo_x4 = (int8_t)(_prefs.flood_suppress_snr_lo * 4);
  return neighbours[i].snr >= lo_x4;
#else
  return false;
#endif
}

// Part 3: should neighbours[i] be EXCLUDED from the flood-suppression protection set? True when M
// cannot transmit-reach it -- inferred from coverage-TRACE first-hop outcomes (M->N is never
// measured directly). A confirmed link (a [N,*] trace returned within M_REACH_RECONFIRM_MS) is
// protected: sticky -- never excluded on later transient timeouts, so no starvation regression vs
// today. An unconfirmed (or aged) link with >= M_REACH_UNREACHABLE_TIMEOUTS consecutive first-hop
// timeouts is M-unreachable: M owes it no coverage (M's rebroadcast never reached it anyway), so it
// must not force a futile self-forward or block suppression.
bool MyMesh::isExcludedFromProtection(int i, uint32_t now_ms) const {
#if MAX_NEIGHBOURS
  const NeighbourInfo& ni = neighbours[i];
  bool confirmed = ni.m_reach_confirmed &&
                   (uint32_t)(now_ms - ni.m_reach_last_ok_ms) < M_REACH_RECONFIRM_MS;   // aging
  if (confirmed) return false;                                  // M->i known good -> protect
  return ni.m_reach_timeouts >= M_REACH_UNREACHABLE_TIMEOUTS;   // never confirmed (or aged) & failing
#else
  (void)i; (void)now_ms; return false;
#endif
}

// Return the index of a NEAR neighbour whose path-hash matches (or -1). A
// forwarded flood carries only forwarder path hashes, so this matches known
// neighbours only (cannot seed new ones -- same limit as touchNeighbourByHash).
int8_t MyMesh::findNearNeighbour(const uint8_t* h, uint8_t hs, uint32_t now) const {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (!isNearNeighbour(i, now)) continue;
    if (neighbours[i].id.isHashMatch(h, hs)) return (int8_t)i;
  }
#endif
  return -1;
}

// Fill out[] with up to max_n near-neighbour INDICES, strongest SNR first (stable on
// ties by index). Near = isNearNeighbour (fresh + SNR>=snr_lo). Coverage is only
// guaranteed for this capped strongest set (see NEAR_NEIGHBOUR_COVERAGE_CAP): the
// adaptive C threshold still counts ALL fresh neighbours for density, so it is
// unaffected. O(MAX_NEIGHBOURS * max_n).
uint8_t MyMesh::topNearNeighbours(int8_t out[], uint8_t max_n, uint32_t now) const {
#if MAX_NEIGHBOURS
  uint8_t n = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (!isNearNeighbour(i, now)) continue;
    int8_t s_i = neighbours[i].snr;
    uint8_t pos = n;
    while (pos > 0 && neighbours[out[pos - 1]].snr < s_i) {   // insertion sort, desc
      if (pos < max_n) out[pos] = out[pos - 1];
      pos--;
    }
    if (pos < max_n) out[pos] = (int8_t)i;
    if (n < max_n) n++;
  }
  return n;
#else
  return 0;
#endif
}

// Index (into neighbours[]) of a top-N peer whose hash matches, else -1.
int8_t MyMesh::findInTopNear(const uint8_t* h, uint8_t hs, const int8_t* top, uint8_t top_n) const {
#if MAX_NEIGHBOURS
  for (uint8_t k = 0; k < top_n; k++) {
    if (neighbours[top[k]].id.isHashMatch(h, hs)) return top[k];
  }
#else
  (void)h; (void)hs; (void)top; (void)top_n;
#endif
  return -1;
}

// True iff at least one top-N near neighbour exists AND every CURRENT top-N near
// neighbour is recorded as covered in e. Only the capped strongest set is checked:
// a rank-(cap+1) neighbour is not owed coverage (deliberate trade-off).
bool MyMesh::allNearNeighboursCovered(const FloodSuppressionEntry& e, uint32_t now) const {
#if MAX_NEIGHBOURS
  int8_t top[NEAR_NEIGHBOUR_COVERAGE_CAP];
  uint8_t n = topNearNeighbours(top, NEAR_NEIGHBOUR_COVERAGE_CAP, now);
  uint8_t prot = 0;                       // protected (M-reachable) peers M owes coverage
  for (uint8_t k = 0; k < n; k++) {
    if (isExcludedFromProtection(top[k], millis())) continue;   // Part 3: M can't reach -> not owed
    prot++;
    if (!e.covers((uint8_t)top[k])) return false;
  }
  return prot > 0;
#else
  (void)e; (void)now; return false;
#endif
}

// Is there a FRESH DIRECTED reach edge from neighbours[from_i] to neighbours[to_j]?
// (to_j heard from_i's transmissions.) DIRECTIONAL: RF links can be asymmetric, and
// we must not infer "to_j heard from_i" from a reverse observation. Used to infer
// coverage: a forwarder fi covers its 1-hop graph neighbours (fi reaches N). Keyed
// by path hash, so robust to LRU reordering of neighbours[]. hs is the hash width
// (the canonical TRACE_MEAS_HASH_SIZE for measured edges). NOTE: freshness is checked
// in MILLIS (the table's TTL is ms-based and addEdge/purge use millis()), NOT in the
// RTC seconds the callers use for near-neighbour freshness.
bool MyMesh::nearReaches(int from_i, int to_j, uint8_t hs) const {
#if MAX_NEIGHBOURS
  uint8_t hfrom[MAX_HASH_SIZE], hto[MAX_HASH_SIZE];
  neighbours[from_i].id.copyHashTo(hfrom, hs);
  neighbours[to_j].id.copyHashTo(hto, hs);
  return _nbr_links.hasEdge(hfrom, hto, hs, millis());
#else
  return false;
#endif
}

// Client-aware suppression gate. ALWAYS active: a dense mesh always has clients
// (possibly unlearned), so there is NO "empty set -> suppress everything" fallback.
// Returns true = "suppressing this flood is safe for attached clients".
//   Tier A (TRACE/CONTROL):   pure infrastructure -> clients never need -> suppress OK.
//   Tier C (REQ/RESPONSE/TXT_MSG/PATH/ANON_REQ): addressed -> forward iff dest is an
//                             attached client, so suppress OK iff dest is NOT one.
//   Tier B (ADVERT/GRP_*/ACK/MULTIPART/...): broadcast, can't address-check, clients may
//                             need -> NEVER suppress (always forward).
bool MyMesh::clientProtectionAllowsSuppress(const mesh::Packet* pkt, uint32_t now) const {
  uint8_t pt = pkt->getPayloadType();
  if (pt == PAYLOAD_TYPE_TRACE || pt == PAYLOAD_TYPE_CONTROL) return true;          // Tier A
  if (pt == PAYLOAD_TYPE_REQ || pt == PAYLOAD_TYPE_RESPONSE || pt == PAYLOAD_TYPE_TXT_MSG ||
      pt == PAYLOAD_TYPE_PATH || pt == PAYLOAD_TYPE_ANON_REQ) {
    if (pkt->payload_len < 1) return false;                                         // malformed -> forward (safe)
    return !attachedClientMatches(pkt->payload[0], now);                            // Tier C
  }
  return false;                                                                     // Tier B
}

// --- Active TRACE coverage measurement ----------------------------------------
// Send one round-trip coverage TRACE: visit-list [a, b, self] with 2-byte hashes.
// It walks self->a->b->self; the SNR measured at b of a's forward (path_snrs[1])
// tells whether b can hear a (a reaches b). TRACE_FLAG_TERMINATE_AT_LAST makes it
// deliver onTraceRecv back HERE (at self) instead of at a bystander. Returns the
// trace tag (0 if the packet pool was full).
uint32_t MyMesh::sendCoverageTrace(const mesh::Identity& a, const mesh::Identity& b) {
  const uint8_t psz = TRACE_MEAS_HASH_SIZE;            // 2 bytes
  uint8_t visit[3 * MAX_HASH_SIZE];
  uint8_t n = 0;
  a.copyHashTo(&visit[n], psz);     n += psz;          // hop 0: a (reacher)
  b.copyHashTo(&visit[n], psz);     n += psz;          // hop 1: b (reached)
  self_id.copyHashTo(&visit[n], psz); n += psz;        // hop 2: self (terminator -> result returns here)

  uint8_t path_sz_code = 1;                            // 1<<1 == 2 bytes
  uint32_t tag = _trace_tag_next++;
  uint8_t flags = path_sz_code | TRACE_FLAG_TERMINATE_AT_LAST;
  mesh::Packet* pkt = createTrace(tag, 0, flags);
  if (!pkt) return 0;
  sendDirect(pkt, visit, n);                           // appends visit-list to payload, pri 5
  return tag;
}

// A coverage TRACE we initiated has returned. Record the measured directed edge
// a->b (path_hashes[0..psz)=a, [psz..2psz)=b) iff the link is strong enough (SNR at
// b of a's forward >= snr_lo), then retire the pending entry. Only our own [a,b,self]
// traces reach us here: every node terminates at itself, so onTraceRecv fires at the
// initiator, never at a relay or bystander.
void MyMesh::onTraceRecv(mesh::Packet* /*packet*/, uint32_t tag, uint32_t /*auth_code*/, uint8_t flags,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
#if MAX_NEIGHBOURS
  uint8_t path_sz = flags & 0x03;
  uint8_t entry_sz = 1 << path_sz;
  uint8_t n_hops = path_len >> path_sz;                // == number of SNRs collected
  if (n_hops == 3 && entry_sz == TRACE_MEAS_HASH_SIZE) {
    _meas_returned++;                                  // a coverage TRACE round-trip completed back here
    int8_t snr_x4 = (int8_t)path_snrs[1];              // SNR at b of a's forward = a reaches b
    if (snr_x4 >= (int8_t)(_prefs.flood_suppress_snr_lo * 4)) {
      _nbr_links.addEdge(path_hashes, path_hashes + entry_sz, entry_sz, millis());
      _meas_edge++;                                    // ...and the a->b link was strong enough to record
    } else {
      // Returned but weak: the a->b link exists yet cannot carry coverage. Cache as no-edge so it
      // is not re-probed every tick; it retries on a per-pair exponential backoff (capped ~10h).
      _nbr_links.addNegative(path_hashes, path_hashes + entry_sz, entry_sz, millis());
      _meas_neg++;
    }
    // Part 3: this trace returned, so its FIRST HOP a (= path_hashes) received M's TX -> M->a
    // works. Confirm a so it is never excluded from the protection set on later transient
    // first-hop timeouts (sticky-confirm with aging -- see isExcludedFromProtection).
    int8_t ia = findNearNeighbour(path_hashes, TRACE_MEAS_HASH_SIZE, getRTCClock()->getCurrentTime());
    if (ia >= 0) {
      neighbours[ia].m_reach_confirmed = true;
      neighbours[ia].m_reach_timeouts = 0;
      neighbours[ia].m_reach_last_ok_ms = millis();
    }
  }
  for (uint8_t i = 0; i < TRACE_PENDING_MAX; i++) {    // retire the matching pending entry (success)
    if (_trace_pending[i].active && _trace_pending[i].tag == tag) { _trace_pending[i].active = false; break; }
  }
#else
  (void)tag; (void)flags; (void)path_snrs; (void)path_hashes; (void)path_len;
#endif
}

// Cadenced coverage measurement. (1) sweep in-flight traces for timeout + single
// retry; (2) every ~60s (~10-15s in sim) find top-N coverage peers whose directed
// edges are missing/expired and probe them with [a,b,self] traces. Bounded by
// TRACE_PENDING_MAX in flight; jittered so simultaneously-booted nodes don't all
// probe at once. TX power is lowered for the burst window (near links are strong)
// and restored afterwards.
void MyMesh::stepCoverageMeasurement() {
#if MAX_NEIGHBOURS
  uint32_t now = millis();

  // restore normal TX power once the burst window has elapsed
  if (_trace_tx_revert_at && millisHasNowPassed(_trace_tx_revert_at)) {
    radio_driver.setTxPower(_prefs.tx_power_dbm);
    _trace_tx_revert_at = 0;
  }

  // (1) timeout / single-retry sweep
  for (uint8_t i = 0; i < TRACE_PENDING_MAX; i++) {
    if (!_trace_pending[i].active) continue;
    if ((uint32_t)(now - _trace_pending[i].sent_ms) <= TRACE_MEAS_TIMEOUT_MS) continue;
    if (_trace_pending[i].retries < 1) {
      _trace_pending[i].retries = 1;
      int8_t ia = findNearNeighbour(_trace_pending[i].a, TRACE_MEAS_HASH_SIZE, getRTCClock()->getCurrentTime());
      int8_t ib = findNearNeighbour(_trace_pending[i].b, TRACE_MEAS_HASH_SIZE, getRTCClock()->getCurrentTime());
      uint32_t tag = (ia >= 0 && ib >= 0) ? sendCoverageTrace(neighbours[ia].id, neighbours[ib].id) : 0;
      if (tag) { _trace_pending[i].tag = tag; _trace_pending[i].sent_ms = now; _meas_sent++; }
      else _trace_pending[i].active = false;            // pair no longer resolvable -> drop
    } else {
      _trace_pending[i].active = false;                 // second miss -> link does not exist (no edge)
      _meas_timeout++;
      _nbr_links.addNegative(_trace_pending[i].a, _trace_pending[i].b, TRACE_MEAS_HASH_SIZE, now);
      _meas_neg++;                                      // cache no-edge so we don't re-probe every tick
      // Part 3: the FIRST HOP a may be M-unreachable (M->a broken -> the trace never left M, so it
      // timed out regardless of b). Bump a's consecutive-failure count; once it reaches the
      // threshold without ever being confirmed, isExcludedFromProtection drops it from protection.
      int8_t ia = findNearNeighbour(_trace_pending[i].a, TRACE_MEAS_HASH_SIZE, getRTCClock()->getCurrentTime());
      if (ia >= 0 && neighbours[ia].m_reach_timeouts < 255) neighbours[ia].m_reach_timeouts++;
    }
  }

  if (!_prefs.flood_suppress) return;

  // (2) cadenced diff/expiry + send
  if (!millisHasNowPassed(_next_meas_check_ms)) return;
#if SIM_BUILD
  _next_meas_check_ms = futureMillis((int)getRNG()->nextInt(10000, 15000));
#else
  _next_meas_check_ms = futureMillis(60000);
#endif
  if (_meas_jitter_until == 0) {                        // first ever: spread this node's first burst
    _meas_jitter_until = futureMillis((int)getRNG()->nextInt(500, 5000));   // (desyncs simultaneously-booted nodes)
    return;
  }
  if (!millisHasNowPassed(_meas_jitter_until)) return;  // inter-burst backoff (de-conflicts simultaneous nodes)

  int8_t top[NEAR_NEIGHBOUR_COVERAGE_CAP];
  uint8_t top_n = topNearNeighbours(top, NEAR_NEIGHBOUR_COVERAGE_CAP, getRTCClock()->getCurrentTime());
  if (top_n < 2) return;

  // Enumerate the P = top_n*(top_n-1) directed pairs (x != y) as a flat list and scan from a rotating
  // offset (_meas_rr_offset), so we don't fixate on the same first absent pair every tick. Decode pair
  // index p -> (x,y) via x = p/(top_n-1); y = p%(top_n-1); if (y >= x) y++;  (bijection onto x!=y).
  // At most ONE trace is sent per cadence tick.
  uint8_t P = top_n * (top_n - 1);
  uint8_t base = _meas_rr_offset % P;
  uint8_t ha[TRACE_MEAS_HASH_SIZE], hb[TRACE_MEAS_HASH_SIZE];
  bool burst_started = false, stop = false;
  for (uint8_t k = 0; k < P && !stop; k++) {
    uint8_t p = (base + k) % P;
    uint8_t x = p / (top_n - 1);
    uint8_t y = p % (top_n - 1);
    if (y >= x) y++;                                     // skip the x==y diagonal
    neighbours[top[x]].id.copyHashTo(ha, TRACE_MEAS_HASH_SIZE);
    neighbours[top[y]].id.copyHashTo(hb, TRACE_MEAS_HASH_SIZE);
    if (_nbr_links.hasEdge(ha, hb, TRACE_MEAS_HASH_SIZE, now)) continue;        // measured & fresh (positive)
    if (_nbr_links.hasNegative(ha, hb, TRACE_MEAS_HASH_SIZE, now)) continue;    // probed, no edge -> backoff (~10h)
    bool inflight = false;                                                          // already probing this direction?
    for (uint8_t i = 0; i < TRACE_PENDING_MAX && !inflight; i++)
      if (_trace_pending[i].active && memcmp(_trace_pending[i].a, ha, TRACE_MEAS_HASH_SIZE) == 0 && memcmp(_trace_pending[i].b, hb, TRACE_MEAS_HASH_SIZE) == 0) inflight = true;
    if (inflight) continue;
    int8_t slot = -1;                                                               // free pending slot?
    for (uint8_t i = 0; i < TRACE_PENDING_MAX; i++) if (!_trace_pending[i].active) { slot = (int8_t)i; break; }
    if (slot < 0) { stop = true; break; }
    if (!burst_started) {
      burst_started = true;
      if (_prefs.trace_tx_power_dbm != _prefs.tx_power_dbm) {
        radio_driver.setTxPower(_prefs.trace_tx_power_dbm);
        _trace_tx_revert_at = futureMillis(TRACE_TX_POWER_RESTORE_MS);
      }
    }
    uint32_t tag = sendCoverageTrace(neighbours[top[x]].id, neighbours[top[y]].id);
    if (!tag) { stop = true; break; }                                       // pool full -> wait
    _meas_sent++;
    _trace_pending[slot].active = true;
    _trace_pending[slot].retries = 0;
    _trace_pending[slot].tag = tag;
    _trace_pending[slot].sent_ms = now;
    memcpy(_trace_pending[slot].a, ha, TRACE_MEAS_HASH_SIZE);
    memcpy(_trace_pending[slot].b, hb, TRACE_MEAS_HASH_SIZE);
    _meas_rr_offset = (uint8_t)((p + 1) % P);        // next tick starts after the pair just probed
    stop = true; break;                                                     // ONE trace per cadence tick
    // (a pair's two directions are ~180ms*3hops round-trips; sending them
    // back-to-back makes the 2nd collide with the 1st's return relay. Spacing
    // to one-per-tick lets each round trip complete cleanly.)
  }
  if (burst_started) _meas_jitter_until = futureMillis((int)getRNG()->nextInt(500, 3000));
#endif
}

// Seed/refresh a directly-attached leaf client (M is its first hop). Small LRU ring.
// `prefix[0]` is the 1-byte match key; `plen` is how many identity bytes are known
// (4 from an advert, 1 from a message src_hash). On refresh, upgrade the stored
// prefix only if we now know MORE bytes (never downgrade).
void MyMesh::addOrRefreshAttachedClient(const uint8_t* prefix, uint8_t plen, uint32_t now) {
  uint8_t h1 = prefix[0];
  for (int i = 0; i < MAX_ATTACHED_CLIENTS; i++) {           // refresh existing (match on prefix[0])
    if (_attached[i].active && _attached[i].prefix[0] == h1) {
      _attached[i].last_seen = now;
      if (plen > _attached[i].prefix_len) {
        memcpy(_attached[i].prefix, prefix, plen);
        _attached[i].prefix_len = plen;
      }
      return;
    }
  }
  int slot = 0; uint32_t oldest = 0xFFFFFFFF;                // else reuse inactive or oldest
  for (int i = 0; i < MAX_ATTACHED_CLIENTS; i++) {
    if (!_attached[i].active) { slot = i; break; }
    if (_attached[i].last_seen < oldest) { oldest = _attached[i].last_seen; slot = i; }
  }
  memcpy(_attached[slot].prefix, prefix, plen);
  _attached[slot].prefix_len = plen;
  _attached[slot].last_seen = now;
  _attached[slot].active = true;
}

bool MyMesh::attachedClientMatches(uint8_t hash1, uint32_t now) const {
  for (int i = 0; i < MAX_ATTACHED_CLIENTS; i++) {
    if (_attached[i].active && _attached[i].prefix[0] == hash1 &&
        (uint32_t)(now - _attached[i].last_seen) <= ATTACHED_CLIENT_FRESH_S) {
      return true;
    }
  }
  return false;
}

void MyMesh::removeAttachedClient(uint8_t hash1) {
  for (int i = 0; i < MAX_ATTACHED_CLIENTS; i++) {
    if (_attached[i].active && _attached[i].prefix[0] == hash1) _attached[i].active = false;
  }
}

void MyMesh::purgeAttachedClients(uint32_t now) {
  for (int i = 0; i < MAX_ATTACHED_CLIENTS; i++) {
    if (_attached[i].active && (uint32_t)(now - _attached[i].last_seen) > ATTACHED_CLIENT_FRESH_S) {
      _attached[i].active = false;
    }
  }
}

// Does this 1-byte hash match a known REPEATER neighbour? (Used to avoid seeding a
// repeater as a client; repeaters are handled by the coverage test, not client-protection.)
bool MyMesh::isKnownRepeaterHash1(uint8_t hash1) const {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (neighbours[i].heard_timestamp != 0 && neighbours[i].id.pub_key[0] == hash1) return true;
  }
#endif
  return false;
}

uint8_t MyMesh::handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood) {
  ClientInfo* client = NULL;
  if (data[0] == 0) {   // blank password, just check if sender is in ACL
    client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    if (client == NULL) {
    #if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Login, sender not in ACL");
    #endif
    }
  }
  if (client == NULL) {
    uint8_t perms;
    if (strcmp((char *)data, _prefs.password) == 0) { // check for valid admin password
      perms = PERM_ACL_ADMIN;
    } else if (strcmp((char *)data, _prefs.guest_password) == 0) { // check guest password
      perms = PERM_ACL_GUEST;
    } else {
#if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Invalid password: %s", data);
#endif
      return 0;
    }

    client = acl.putClient(sender, 0);  // add to contacts (if not already known)
    if (sender_timestamp <= client->last_login_timestamp) {
      MESH_DEBUG_PRINTLN("Possible login replay attack!");
      return 0;  // FATAL: client table is full -OR- replay attack
    }

    MESH_DEBUG_PRINTLN("Login success!");
    client->last_login_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions &= ~0x03;
    client->permissions |= perms;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

    if (perms != PERM_ACL_GUEST) {   // keep number of FS writes to a minimum
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }
  }

  if (is_flood) {
    client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  reply_data[4] = RESP_SERVER_LOGIN_OK;
  reply_data[5] = 0;  // Legacy: was recommended keep-alive interval (secs / 16)
  reply_data[6] = client->isAdmin() ? 1 : 0;
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
  reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

  return 13;  // reply length
}

uint8_t MyMesh::handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)

    return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    sprintf((char *) &reply_data[8], "%s\n%s", _prefs.node_name, _prefs.owner_info);

    return 8 + strlen((char *) &reply_data[8]);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    reply_data[8] = 0;  // features
#ifdef WITH_RS232_BRIDGE
    reply_data[8] |= 0x01;  // is bridge, type UART
#elif WITH_ESPNOW_BRIDGE
    reply_data[8] |= 0x03;  // is bridge, type ESP-NOW
#endif
    if (_prefs.disable_fwd) {   // is this repeater currently disabled
      reply_data[8] |= 0x80;  // is disabled
    }
    // TODO:  add some kind of moving-window utilisation metric, so can query 'how busy' is this repeater
    return 9;   // reply length
  }
  return 0;
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {  // guests can also access this now
    RepeaterStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundTotal();
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.total_rx_air_time_secs = getReceiveAirTime() / 1000;
    stats.n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&reply_data[4], &stats, sizeof(stats));

    return 4 + sizeof(stats); //  reply_len
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);

    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

	// This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (c->permissions == 0) continue;  // skip deleted entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  if (payload[0] == REQ_TYPE_GET_NEIGHBOURS) {
    uint8_t request_version = payload[1];
    if (request_version == 0) {

      // reply data offset (after response sender_timestamp/tag)
      int reply_offset = 4;

      // get request params
      uint8_t count = payload[2]; // how many neighbours to fetch (0-255)
      uint16_t offset;
      memcpy(&offset, &payload[3], 2); // offset from start of neighbours list (0-65535)
      uint8_t order_by = payload[5]; // how to order neighbours. 0=newest_to_oldest, 1=oldest_to_newest, 2=strongest_to_weakest, 3=weakest_to_strongest
      uint8_t pubkey_prefix_length = payload[6]; // how many bytes of neighbour pub key we want
      // we also send a 4 byte random blob in payload[7...10] to help packet uniqueness

      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS count=%d, offset=%d, order_by=%d, pubkey_prefix_length=%d", count, offset, order_by, pubkey_prefix_length);

      // clamp pub key prefix length to max pub key length
      if(pubkey_prefix_length > PUB_KEY_SIZE){
        pubkey_prefix_length = PUB_KEY_SIZE;
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS invalid pubkey_prefix_length=%d clamping to %d", pubkey_prefix_length, PUB_KEY_SIZE);
      }

      // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
      int16_t neighbours_count = 0;
#if MAX_NEIGHBOURS
      NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        auto neighbour = &neighbours[i];
        if (neighbour->heard_timestamp > 0) {
          sorted_neighbours[neighbours_count] = neighbour;
          neighbours_count++;
        }
      }

      // sort neighbours based on order
      if (order_by == 0) {
        // sort by newest to oldest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting newest to oldest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp > b->heard_timestamp; // desc
        });
      } else if (order_by == 1) {
        // sort by oldest to newest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting oldest to newest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp < b->heard_timestamp; // asc
        });
      } else if (order_by == 2) {
        // sort by strongest to weakest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting strongest to weakest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr > b->snr; // desc
        });
      } else if (order_by == 3) {
        // sort by weakest to strongest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting weakest to strongest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr < b->snr; // asc
        });
      }
#endif

      // build results buffer
      int results_count = 0;
      int results_offset = 0;
      uint8_t results_buffer[130];
      for(int index = 0; index < count && index + offset < neighbours_count; index++){
        
        // stop if we can't fit another entry in results
        int entry_size = pubkey_prefix_length + 4 + 1;
        if(results_offset + entry_size > sizeof(results_buffer)){
          MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS no more entries can fit in results buffer");
          break;
        }

#if MAX_NEIGHBOURS
        // add next neighbour to results
        auto neighbour = sorted_neighbours[index + offset];
        uint32_t heard_seconds_ago = safeElapsedSecs(getRTCClock()->getCurrentTime(), neighbour->heard_timestamp);
        memcpy(&results_buffer[results_offset], neighbour->id.pub_key, pubkey_prefix_length); results_offset += pubkey_prefix_length;
        memcpy(&results_buffer[results_offset], &heard_seconds_ago, 4); results_offset += 4;
        memcpy(&results_buffer[results_offset], &neighbour->snr, 1); results_offset += 1;
        results_count++;
#endif

      }

      // build reply
      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS neighbours_count=%d results_count=%d", neighbours_count, results_count);
      memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_buffer, results_offset); reply_offset += results_offset;

      return reply_offset;
    }
  } else if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
    sprintf((char *) &reply_data[4], "%s\n%s\n%s", FIRMWARE_VERSION, _prefs.node_name, _prefs.owner_info);
    return 4 + strlen((char *) &reply_data[4]);
  }
  return 0; // unknown command
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_REPEATER, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

static uint8_t max_loop_minimal[] =  { 0, /* 1-byte */  4, /* 2-byte */  2, /* 3-byte */  1 };
static uint8_t max_loop_moderate[] = { 0, /* 1-byte */  2, /* 2-byte */  1, /* 3-byte */  1 };
static uint8_t max_loop_strict[] =   { 0, /* 1-byte */  1, /* 2-byte */  1, /* 3-byte */  1 };

bool MyMesh::isLooped(const mesh::Packet* packet, const uint8_t max_counters[]) {
  uint8_t hash_size = packet->getPathHashSize();
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {      // count how many times this node is already in the path
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= max_counters[hash_size];
}

void MyMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size) {
  if (recv_pkt_region && !recv_pkt_region->isWildcard()) {  // if _request_ packet scope is known, send reply with same scope
    TransportKey scope;
    if (region_map.getTransportKeysFor(*recv_pkt_region, &scope, 1) > 0) {
      sendFloodScoped(scope, packet, delay_millis, path_hash_size);
    } else {
      sendFlood(packet, delay_millis, path_hash_size);  // send un-scoped
    }
  } else {
    sendFlood(packet, delay_millis, path_hash_size);  // send un-scoped
  }
}

void MyMesh::cancelPendingFloodOutbound(const uint8_t* hash) {
  // Remove our own already-scheduled flood rebroadcast for this hash (if any).
  // At most one such outbound exists per flood; the hash is path-independent,
  // so it matches the inbound copies we counted.
  int n = _mgr->getOutboundTotal();
  for (int i = 0; i < n; i++) {
    mesh::Packet* p = _mgr->getOutboundByIdx(i);
    if (p && p->isRouteFlood()) {
      const uint8_t* h = p->calculatePacketHash();
      if (memcmp(h, hash, MAX_HASH_SIZE) == 0) {
        mesh::Packet* removed = _mgr->removeOutboundByIdx(i);
        if (removed) releasePacket(removed);   // return to pool
        return;   // a node schedules at most one rebroadcast per flood
      }
    }
  }
}

// Static C used when the neighbour table is unavailable (no MAX_NEIGHBOURS, cold start, or no
// fresh neighbours yet): a moderate threshold — the counter still won't fire for genuinely sparse
// nodes (too few overheard forwards reach it), so this is safe as a zero-admin default.
static const uint8_t FLOOD_SUPPRESS_FALLBACK_C = 2;

// Effective params: the master switch gates everything; adaptive values apply when neighbour data
// is available, otherwise the static fallback (configured snr_hi/lo/delay + FLOOD_SUPPRESS_FALLBACK_C).
uint8_t MyMesh::effectiveFloodSuppressC() const {
  if (!_prefs.flood_suppress) return 0;
  return _fs_adaptive_active ? _fs_eff_c : FLOOD_SUPPRESS_FALLBACK_C;
}
int8_t MyMesh::effectiveFloodSuppressSnrHi() const {
  if (!_prefs.flood_suppress) return _prefs.flood_suppress_snr_hi;   // moot: effective c == 0
  return _fs_adaptive_active ? _fs_eff_hi : _prefs.flood_suppress_snr_hi;
}

// Derive effective c (from neighbour density) and snr_hi (from link-SNR p75). Runs throttled from
// loop(); sets _fs_adaptive_active. Under #if MAX_NEIGHBOURS (else adaptive stays inactive and
// effectiveFloodSuppressC falls back to FLOOD_SUPPRESS_FALLBACK_C).
void MyMesh::updateAdaptiveFloodParams() {
#if MAX_NEIGHBOURS
  int n = 0;
  int8_t snr_x4[MAX_NEIGHBOURS];
  uint32_t now = getRTCClock()->getCurrentTime();      // seconds (RTC)
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (neighbours[i].heard_timestamp == 0) continue;              // empty slot
    if ((now - neighbours[i].heard_timestamp) > NEIGHBOUR_FRESH_S) continue;  // stale
    snr_x4[n++] = neighbours[i].snr;                     // stored x4
  }

  if (n < 1) {
    _fs_adaptive_active = false;                        // no fresh neighbours -> static fallback
    return;
  }
  _fs_adaptive_active = true;

  // c from density: <3 fresh => 0 (edge node, don't suppress); 3-4 => 3; >=5 => 2.
  uint8_t derived_c = (n < 3) ? 0 : (n <= 4) ? 3 : 2;

  // snr_hi = p75 of fresh link SNRs (dB), clamped to [lo+4, lo+12]; needs >=4 samples.
  int8_t derived_hi = _prefs.flood_suppress_snr_hi;     // else keep configured
  if (n >= 4) {
    for (int i = 1; i < n; i++) {                        // insertion sort (<=50 elems)
      int8_t v = snr_x4[i]; int j = i - 1;
      while (j >= 0 && snr_x4[j] > v) { snr_x4[j + 1] = snr_x4[j]; j--; }
      snr_x4[j + 1] = v;
    }
    int8_t hi_db = (int8_t)(snr_x4[((n - 1) * 3) / 4] / 4);   // p75, x4 -> dB
    int8_t lo = _prefs.flood_suppress_snr_lo;
    if (hi_db < lo + 4) hi_db = lo + 4;
    if (hi_db > lo + 12) hi_db = lo + 12;
    derived_hi = hi_db;
  }

  // Debounce c: adopt a change only after a 2nd confirming cycle (avoid flapping).
  uint8_t new_c = (derived_c == _fs_pending_c) ? derived_c : _fs_eff_c;
  _fs_pending_c = derived_c;

  if (new_c != _fs_eff_c || derived_hi != _fs_eff_hi) {
    MESH_DEBUG_PRINTLN("%s flood-suppress adaptive: neighbours=%d -> c=%d (was %d), snr_hi=%d (was %d)",
                       getLogDateTime(), n, new_c, _fs_eff_c, (int)derived_hi, (int)_fs_eff_hi);
  }
  _fs_eff_c = new_c;
  _fs_eff_hi = derived_hi;
#else
  _fs_adaptive_active = false;   // no neighbour table compiled in -> static fallback
#endif
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood()) {
    if (effectiveFloodSuppressC() > 0) {
      // If overheard forwards already made our rebroadcast redundant, do not
      // schedule it at all (covers the case where the 2nd copy arrived and was
      // flagged suppressed before the 1st copy was processed/scheduled).
      const uint8_t* hash = packet->calculatePacketHash();
      FloodSuppressionEntry* e = _flood_supp.find(hash, millis());
      if (e && e->suppressed) return false;
    }
    if (packet->getPathHashCount() >= _prefs.flood_max) return false;
    if (packet->getRouteType() == ROUTE_TYPE_FLOOD && packet->getPathHashCount() >= _prefs.flood_max_unscoped) return false;
    if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->getPathHashCount() >= _prefs.flood_max_advert) return false;
  }
  if (packet->isRouteFlood() && recv_pkt_region == NULL) {
    MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
    return false;
  }
  if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
    const uint8_t* maximums;
    if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
      maximums = max_loop_minimal;
    } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
      maximums = max_loop_moderate;
    } else {
      maximums = max_loop_strict;
    }
    if (isLooped(packet, maximums)) {
      MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
      return false;
    }
  }
  return true;
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  Serial.print(getLogDateTime());
  Serial.print(" RAW: ");
  mesh::Utils::printHex(Serial, raw, len);
  Serial.println();
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
  // Refresh known-neighbour liveness from this overheard forward. logRx fires for
  // EVERY received packet (allowPacketForward does not -- it runs only for the
  // first copy), so this is the reliable place to keep heard_timestamp current.
  // Adverts may be hours apart; forwarded floods are frequent, so the neighbour
  // table no longer goes entirely stale between adverts.
  if (pkt->isRouteFlood()) {
    touchNeighbourByHash(pkt);
  }

  // --- Attached-client learning -------------------------------------------
  // A count==0 packet (empty path) means M is the originator's FIRST hop, i.e. the
  // originator is a directly-attached neighbour -- a leaf CLIENT if not a known
  // repeater. Seed/refresh it from the stable src_hash the payload carries
  // (adverts are seeded in onAdvertRecv, which has the parsed identity). Route-type-
  // agnostic: a zero-hop DIRECT packet counts too. Pathed packets (count>0) are
  // ignored -- path[0]==self at every relay makes the originator ambiguous there.
  if (pkt->getPathHashCount() == 0) {
    uint8_t pt = pkt->getPayloadType();
    if ((pt == PAYLOAD_TYPE_REQ || pt == PAYLOAD_TYPE_RESPONSE || pt == PAYLOAD_TYPE_TXT_MSG || pt == PAYLOAD_TYPE_PATH)
        && pkt->payload_len >= 2) {
      uint8_t h1 = pkt->payload[1];          // src_hash (originator == attached client)
      if (!isKnownRepeaterHash1(h1)) addOrRefreshAttachedClient(&h1, 1, getRTCClock()->getCurrentTime());
    }
  }

  // --- Coverage-test FLOOD suppression (graph-reach) ----------------------
  // M suppresses its rebroadcast of F iff every NEAR neighbour is already known
  // to have F. A neighbour is covered if it FORWARDED F (it is on an overheard
  // path -- certain) OR if it was REACHED by a near forwarder fi that has a fresh
  // inter-neighbour edge fi<->N (N heard fi's forward -- inferred). Coverage
  // accumulates across overheard forwards, so combined reach can cover everyone.
  // Runs at RX-arrival (before scheduling), so a later overheard copy can cancel
  // a pending rebroadcast early (allowPacketForward also early-outs suppressed
  // entries for the copy-before-decision ordering).
  if (effectiveFloodSuppressC() > 0 && pkt->isRouteFlood()) {
    const uint8_t* hash = pkt->calculatePacketHash();
    bool is_new = false;
    FloodSuppressionEntry* e = _flood_supp.touch(hash, millis(), &is_new);
    if (e && !e->suppressed) {
      if (is_new) _fs_seen++;                // distinct flood heard -> candidate for our rebroadcast
#if MAX_NEIGHBOURS
      uint32_t now = getRTCClock()->getCurrentTime();   // seconds (RTC), for near-neighbour freshness
      uint8_t hs = pkt->getPathHashSize();
      uint8_t count = pkt->getPathHashCount();
      const uint8_t* p = pkt->path;

      // The reach graph (_nbr_links) is now populated by ACTIVE TRACE measurement
      // (stepCoverageMeasurement), NOT inferred from this flood's path. So here we
      // only record which COVERAGE peers (M's top-N strongest near neighbours)
      // forwarded F, then read the measured graph to infer who else was reached.
      int8_t top[NEAR_NEIGHBOUR_COVERAGE_CAP];
      uint8_t top_n = topNearNeighbours(top, NEAR_NEIGHBOUR_COVERAGE_CAP, now);

      // Which NEAR neighbours forwarded F. Uses findNearNeighbour (ALL near, not just top-N) so
      // a rank-(cap+1) forwarder with a harvested cross-rank edge to a top-N neighbour can still
      // mark it covered in (b). forwarded[f] true => f is a near neighbour that forwarded F.
      bool forwarded[MAX_NEIGHBOURS] = { false };
      for (uint8_t k = 0; k < count; k++) {
        int8_t idx = findNearNeighbour(p, hs, now);
        if (idx >= 0) forwarded[idx] = true;          // this near neighbour forwarded F => has F
        p += hs;
      }

      // (b) COVERAGE: a protected peer j has F if it forwarded F (certain), or if any near
      //     forwarder f REACHES it via a fresh measured/harvested edge f->j (j heard f's forward
      //     => j has F, inferred). Edges at the canonical TRACE hash width. This is where a
      //     harvested cross-rank edge f(rank>cap)->j(top-N) first pays off (Part 2).
      for (uint8_t a = 0; a < top_n; a++) {
        int j = top[a];
        if (forwarded[j]) { e->addCovered((uint8_t)j); continue; }   // j forwarded F => has F (certain)
        for (int f = 0; f < MAX_NEIGHBOURS; f++) {                   // any near forwarder reaches j?
          if (f == j || !forwarded[f]) continue;
          if (nearReaches(f, j, TRACE_MEAS_HASH_SIZE)) { e->addCovered((uint8_t)j); break; }
        }
      }

      // (c) MUST-COVER-SELF: a protected non-forwarder i that NO near forwarder reaches can only
      //     be covered by M's own TX -> M must forward. (Cold-start: graph empty before any TRACE
      //     completes, so every i is unreachable -> M forwards, as intended.) Part 3: skip i that
      //     M cannot transmit-reach -- M's TX would never reach it anyway, so it must not force a
      //     futile self-forward (nor block suppression for the peers M CAN reach).
      e->must_cover_self = false;
      for (uint8_t a = 0; a < top_n && !e->must_cover_self; a++) {
        int i = top[a];
        if (isExcludedFromProtection(i, millis())) continue;   // Part 3: M->i broken -> not owed coverage
        if (forwarded[i]) continue;                            // i forwarded F -> covered
        bool reachable = false;
        for (int f = 0; f < MAX_NEIGHBOURS; f++) {             // does any near forwarder f reach i?
          if (f == i || !forwarded[f]) continue;
          if (nearReaches(f, i, TRACE_MEAS_HASH_SIZE)) { reachable = true; break; }
        }
        if (!reachable) e->must_cover_self = true;
      }

      // (d) suppress iff no isolated-uncovered peer, every coverage peer covered, and
      //     client-protection allows it (3-tier, always active).
      if (!e->must_cover_self && allNearNeighboursCovered(*e, now)
          && clientProtectionAllowsSuppress(pkt, now)) {
        e->suppressed = true;
        _fs_suppressed++;                    // our rebroadcast was made redundant
        cancelPendingFloodOutbound(hash);
      }
#endif
    }
  }
#if MAX_NEIGHBOURS
  // --- Passive TRACE harvest (Part 2) -----------------------------------------
  // Overhear a coverage TRACE [a,b,initiator] that another repeater emitted for ITS own suppression
  // and adopt its measured a->b edge -- a richer reach graph at 0 extra airtime (TRACEs are
  // cleartext; logRx fires for every received packet). Only the FINAL leg carries path[1] = the
  // a->b SNR (b just appended it), so gate on getPathHashCount()>=2. Same SNR gate / direction /
  // width / negative-on-weak as onTraceRecv; skip our own trace and any whose a/b are not both ours.
  if (effectiveFloodSuppressC() > 0 && pkt->isRouteDirect()
      && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE
      && pkt->payload_len >= 9 + 3 * TRACE_MEAS_HASH_SIZE) {
    uint8_t flags = pkt->payload[8];
    uint8_t entry_sz = 1 << (flags & 0x03);
    if ((flags & TRACE_FLAG_TERMINATE_AT_LAST) && entry_sz == TRACE_MEAS_HASH_SIZE
        && (pkt->payload_len - 9) / entry_sz == 3                     // exactly [a,b,initiator]
        && pkt->getPathHashCount() >= 2) {                            // final leg: path[1] = a->b SNR
      const uint8_t* visit = pkt->payload + 9;                        // [a(2), b(2), initiator(2)]
      if (!self_id.isHashMatch(visit + 2 * entry_sz, entry_sz)) {     // not our own trace
        uint32_t now = getRTCClock()->getCurrentTime();
        if (findNearNeighbour(visit, entry_sz, now) >= 0
            && findNearNeighbour(visit + entry_sz, entry_sz, now) >= 0) {   // both a,b are our near
          int8_t snr_ab_x4 = (int8_t)pkt->path[1];
          if (snr_ab_x4 >= (int8_t)(_prefs.flood_suppress_snr_lo * 4)) {
            _nbr_links.addEdge(visit, visit + entry_sz, entry_sz, millis());   // a reaches b (clears stale neg)
            _meas_harvested++;
          } else {
            _nbr_links.addNegative(visit, visit + entry_sz, entry_sz, millis());
            _meas_harvest_neg++;
          }
        }
      }
    }
  }
#endif
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 1) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTx(mesh::Packet *pkt, int len) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 0) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  uint32_t delay = getRNG()->nextInt(0, 5*t + 1);
  // Central flood relays (strong RX SNR) wait longer -> wider window to observe
  // overheard forwards and be cancelled as redundant. Edge relays keep the short
  // delay so they extend reach quickly. Skip the widening when M must forward
  // regardless (an isolated, uncovered near neighbour) -- waiting cannot change
  // that outcome, so forward at the base delay.
  if (effectiveFloodSuppressC() > 0 && packet->isRouteFlood()
      && packet->getSNR() >= effectiveFloodSuppressSnrHi()) {
    const uint8_t* hash = packet->calculatePacketHash();
    FloodSuppressionEntry* e = _flood_supp.find(hash, millis());
    if (!(e && e->must_cover_self)) {
      delay *= (1 + _prefs.flood_suppress_delay_x);
    }
  }
  return delay;
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

bool MyMesh::isResendChannelActive() {
  // Non-invasive resend LBT (NO CAD, so RX stays open to overhear the downstream forward
  // and cancel the resend). Block when a LoRa preamble/header is being received (often that
  // forward) OR the live channel energy is well above the noise floor (foreign interference).
  int margin = (int)radio_driver.getCurrentRSSI() - _radio->getNoiseFloor();
  return radio_driver.isReceivingPacket() || (margin >= RESEND_INTERFERENCE_MARGIN);
}

mesh::DispatcherAction MyMesh::onRecvPacket(mesh::Packet* pkt) {
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
  return Mesh::onRecvPacket(pkt);
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;  // ensure null terminator
    uint8_t reply_len;

    reply_path_len = 0xFF;
    if (data[4] == 0 || data[4] >= ' ') {   // is password, ie. a login request
      reply_len = handleLoginReq(sender, secret, timestamp, &data[4], packet->isRouteFlood());
    } else if (data[4] == ANON_REQ_TYPE_REGIONS && packet->isRouteDirect()) {
      reply_len = handleAnonRegionsReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_OWNER && packet->isRouteDirect()) {
      reply_len = handleAnonOwnerReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_BASIC && packet->isRouteDirect()) {
      reply_len = handleAnonClockReq(sender, timestamp, &data[5]);
    } else {
      reply_len = 0;  // unknown/invalid request type
    }

    if (reply_len == 0) return;   // invalid request

    if (packet->isRouteFlood()) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet* path = createPathReturn(sender, secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
      if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    } else if (reply_path_len == 0xFF) {
      mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
      if (reply) sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    } else {
      mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
      if (reply) sendDirect(reply, reply_path, reply_path_len, SERVER_RESPONSE_DELAY);
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
  for (int i = 0; i < acl.getNumClients(); i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // if this a zero hop advert (and not via 'Share'), classify the originator
  if (packet->getPathHashCount() == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid()) {
      if (parser.getType() == ADV_TYPE_REPEATER) {           // just keep neighbouring Repeaters
        putNeighbour(id, timestamp, packet->getSNR());
        uint8_t h1; id.copyHashTo(&h1, 1);
        removeAttachedClient(h1);            // reconciled: this node is a repeater, not a client
      } else {                                // CHAT/ROOM/SENSOR/... -> a directly-attached leaf client
        uint8_t p[4]; id.copyHashTo(p, 4);   // advert carries the full identity -> 4-byte prefix
        addOrRefreshAttachedClient(p, 4, getRTCClock()->getCurrentTime());
      }
    }
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  ClientInfo* client = acl.getClientByIdx(i);

  if (type == PAYLOAD_TYPE_REQ) { // request (from a Known admin client!)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    if (timestamp > client->last_timestamp) { // prevent replay attacks
      int reply_len = handleRequest(client, timestamp, &data[4], len - 4);
      if (reply_len == 0) return; // invalid command

      client->last_timestamp = timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
        if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      } else {
        mesh::Packet *reply =
            createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
        if (reply) {
          if (mesh::Packet::isValidPathLen(client->out_path_len)) { // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && client->isAdmin()) { // a CLI command
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported text type received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      if (flags == TXT_TYPE_PLAIN) { // for legacy CLI, send Acks
        uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove
                           // to sender that we got it
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                            PUB_KEY_SIZE);

        mesh::Packet *ack = createAck(ack_hash);
        if (ack) {
          if (mesh::Packet::isValidPathLen(client->out_path_len)) {
            sendDirect(ack, client->out_path, client->out_path_len, TXT_ACK_DELAY);
          } else {
            sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          }
        }
      }

      uint8_t temp[166];
      char *command = (char *)&data[5];
      char *reply = (char *)&temp[5];
      if (is_retry) {
        *reply = 0;
      } else {
        handleCommand(sender_timestamp, client, command, reply);
      }
      int text_len = strlen(reply);
      if (text_len > 0) {
        uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
        if (timestamp == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          timestamp++;
        }
        memcpy(temp, &timestamp, 4);        // mostly an extra blob to help make packet_hash unique
        temp[4] = (TXT_TYPE_CLI_DATA << 2); // NOTE: legacy was: TXT_TYPE_PLAIN

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (mesh::Packet::isValidPathLen(client->out_path_len)) {
            sendDirect(reply, client->out_path, client->out_path_len, CLI_REPLY_DELAY_MILLIS);
          } else {
            sendFloodReply(reply, CLI_REPLY_DELAY_MILLIS, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);

    // store a copy of path, for sendDirect()
    if (client->out_path_len != OUT_PATH_FORCE_FLOOD) {
      client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    }
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  if (type == CTL_TYPE_NODE_DISCOVER_REQ && packet->payload_len >= 6
      && !_prefs.disable_fwd && discover_limiter.allow(rtc_clock.getCurrentTime())
  ) {
    int i = 1;
    uint8_t  filter = packet->payload[i++];
    uint32_t tag;
    memcpy(&tag, &packet->payload[i], 4); i += 4;
    uint32_t since;
    if (packet->payload_len >= i+4) {   // optional since field
      memcpy(&since, &packet->payload[i], 4); i += 4;
    } else {
      since = 0;
    }

    if ((filter & (1 << ADV_TYPE_REPEATER)) != 0 && _prefs.discovery_mod_timestamp >= since) {
      bool prefix_only = packet->payload[0] & 1;
      uint8_t data[6 + PUB_KEY_SIZE];
      data[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;   // low 4-bits for node type
      data[1] = packet->_snr;   // let sender know the inbound SNR ( x 4)
      memcpy(&data[2], &tag, 4);     // include tag from request, for client to match to
      memcpy(&data[6], self_id.pub_key, PUB_KEY_SIZE);
      auto resp = createControlData(data, prefix_only ? 6 + 8 : 6 + PUB_KEY_SIZE);
      if (resp) {
        sendZeroHop(resp, getRetransmitDelay(resp)*4);  // apply random delay (widened x4), as multiple nodes can respond to this
      }
    }
  } else if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, rtc_clock.getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq(uint32_t delay_millis) {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);

  // When scheduled in the future (e.g. fired after the boot advert), add a small random jitter
  // so a fleet reboot doesn't synchronise all discover requests, and shift the reply window
  // past the actual send time so responses arriving after the delayed TX aren't dropped.
  uint32_t effective_delay = delay_millis;
  if (delay_millis > 0) {
    uint8_t jb[1]; getRNG()->random(jb, 1);
    effective_delay += (uint32_t)jb[0] * 16u;   // 0..4080 ms jitter
  }
  pending_discover_until = futureMillis(60000 + effective_delay);

  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt, effective_delay);
  }
}

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this),
      telemetry(MAX_PACKET_PAYLOAD - 4),
      discover_limiter(4, 120),  // max 4 every 2 minutes
      anon_limiter(4, 180)   // max 4 every 3 minutes
#if defined(WITH_RS232_BRIDGE)
      , bridge(&_prefs, WITH_RS232_BRIDGE, _mgr, &rtc)
#endif
#if defined(WITH_ESPNOW_BRIDGE)
      , bridge(&_prefs, _mgr, &rtc)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  _fs_eff_c = 0;                     // adaptive: off until neighbour table fills
  _fs_eff_hi = 9;
  _fs_pending_c = 0;
  _fs_adaptive_active = false;       // until neighbour data is available -> static fallback
  _fs_next_recompute_ms = 0;
  _fs_seen = 0;
  _fs_suppressed = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  set_radio_at = revert_radio_at = 0;
  _logging = false;
  region_load_active = false;
  recv_pkt_region = NULL;

#if MAX_NEIGHBOURS
  memset(neighbours, 0, sizeof(neighbours));
#endif

  // defaults
  _prefs.airtime_factor = 9.0; // EU regulation
  _prefs.rx_delay_base = 0.0f;   // // Default for "Local" class
  _prefs.tx_delay_factor = 0.3f; // Default for "Local" class
  _prefs.direct_tx_delay_factor = 0.1f; // Default for "Local" class
  _prefs.max_resend_attempts = 2;
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.advert_interval = 239;        // 239 minutes
  _prefs.flood_advert_interval = 167; // 167 hours
  _prefs.flood_max = 18; // Default for "Local" class
  _prefs.flood_max_unscoped = 4; // Default for "Local" class
  _prefs.flood_max_advert = 4; // Default for "Local" class
  _prefs.multi_acks = 1;
  _prefs.path_hash_mode = 2; 
  _prefs.loop_detect = LOOP_DETECT_MINIMAL;
  _prefs.agc_reset_interval = 0;
  #if SIM_BUILD
  // SIM ONLY: the simulator accelerates adverts to ~20s (see updateAdvertTimer). At the real
  // default of 8 hops, every advert floods across the whole grid (9 TX/advert in multi_path),
  // saturating the channel so almost no advert survives to seed neighbour tables. Neighbour
  // discovery only needs zero-hop adverts (the originator's direct TX), so limiting advert
  // propagation to 2 hops preserves discovery while cutting advert airtime ~4x. HW unchanged.
  _prefs.flood_max_advert = 2;
#endif
  _prefs.interference_threshold = 0; // disabled
  _prefs.cad_enabled = 1;            // hardware CAD before TX (off by default; 'set cad on')
  _prefs.flood_suppress = 1;          // redundancy-aware flood suppression ON by default (adaptive + static fallback)
  _prefs.flood_suppress_snr_hi = 9;  // dB: strong overheard forward => counts double
  _prefs.flood_suppress_snr_lo = 0;  // dB: weak overheard forward => ignored (preserve edge)
  _prefs.flood_suppress_delay_x = 2; // extra TX-delay multiplier for central flood relays
  _prefs.trace_tx_power_dbm = 10;    // TX power for coverage TRACE probes only (near links are strong; less disturbance)

  // bridge defaults
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 0;    // logTx
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  StrHelper::strncpy(_prefs.bridge_secret, "LVSITANOS", sizeof(_prefs.bridge_secret));

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

  _prefs.adc_multiplier = 0.0f; // 0.0f means use default board multiplier

#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default;
#endif
#endif
  _prefs.radio_fem_rxgain = 1;

  pending_discover_tag = 0;
  pending_discover_until = 0;

  memset(default_scope.key, 0, sizeof(default_scope.key));
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);
  acl.load(_fs, self_id);
  // TODO: key_store.begin();
  region_map.load(_fs);

  // establish default-scope
  {
    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
      region_map.getTransportKeysFor(*r, &default_scope, 1);
    } else {
#ifdef DEFAULT_FLOOD_SCOPE_NAME
      r = region_map.findByName(DEFAULT_FLOOD_SCOPE_NAME);
      if (r == NULL) {
        r = region_map.putRegion(DEFAULT_FLOOD_SCOPE_NAME, 0);  // auto-create the default scope region
        if (r) { r->flags = 0; }   // Allow-flood
      }
      if (r) {
        region_map.setDefaultRegion(r);
        region_map.getTransportKeysFor(*r, &default_scope, 1);
      }
#endif
    }
  }

#if defined(WITH_BRIDGE)
  if (_prefs.bridge_enabled) {
    bridge.begin();
  }
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);

  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);

  updateAdvertTimer();
  updateFloodAdvertTimer();

  board.setAdcMultiplier(_prefs.adc_multiplier);

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#endif
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, path_hash_size);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, path_hash_size);
  }
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#else
#error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
#if SIM_BUILD
  // SIMULATOR ONLY (hardware builds take the #else path unchanged).
  //
  // Two sim-specific reasons the real 2-minute, advert_interval-gated timer does not populate
  // neighbour tables in the simulator:
  //   1. The simulator boots every node at (near) the same instant and drives an ABSOLUTE
  //      firmware clock, so all nodes' adverts fire in the same ~1-2s window and collide at
  //      dense nodes (equal-SNR neighbours, no capture winner) -> ALL discarded.
  //   2. The sim zeros _prefs.advert_interval via prefs-save validation (the 2-minute default
  //      fails the "manually configured" < 60-minute check) after the first advert cycle, which
  //      would halt adverts entirely under the real advert_interval-gated path.
  // Fix: schedule UNCONDITIONALLY at an accelerated (~20s), per-node-randomised cadence. Real
  // hardware desyncs naturally via independent clocks; this emulates that for observable
  // coverage dynamics. Independent of advert_interval so the zeroing cannot stop it.
  // ~60s cadence (avg): enough rounds to populate within ~420s while keeping advert airtime
  // low in a dense grid. (Local adverts use sendZeroHop = 1 TX each, no forwarding; still, in a
  // 9-node all-hears-all grid the sim's any-overlap/<6dB collision model is harsh, so a 20s
  // cadence saturated the channel. ~60s is the sweet spot for multi_path.)
  next_local_advert = futureMillis(getRNG()->nextInt(30000, 90000));
#else
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis(((uint32_t)_prefs.advert_interval) * 2 * 60 * 1000);
  } else {
    next_local_advert = 0; // stop the timer
  }
#endif
}

void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}

void MyMesh::setTxPower(int8_t power_dbm) {
  radio_driver.setTxPower(power_dbm);
}

bool MyMesh::setRxBoostedGain(bool enable) {
  return radio_driver.setRxBoostedGainMode(enable);
}

#if defined(USE_LR2021)
bool MyMesh::configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
  return radio_driver.configSideDetectors(sideDetSFs, num, bw);
}
#endif

void MyMesh::formatNeighborsReply(char *reply) {
  char *dp = reply;

#if MAX_NEIGHBOURS
  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = safeElapsedSecs(getRTCClock()->getCurrentTime(), neighbour->heard_timestamp);
    sprintf(dp, "%s:%d:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
#endif
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#endif
}

void MyMesh::startRegionsLoad() {
  temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
  memset(load_stack, 0, sizeof(load_stack));
  load_stack[0] = &temp_map.getWildcard();
  region_load_active = true;
}

bool MyMesh::saveRegions() {
  return region_map.save(_fs);
}

void MyMesh::onDefaultRegionChanged(const RegionEntry* r) {
  if (r) {
    region_map.getTransportKeysFor(*r, &default_scope, 1);
  } else {
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(),
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::formatResendRatioReply(char *reply) {
  if (!_prefs.max_resend_attempts) return;  // plain "> 0" when resends are disabled
  StatsFormatHelper::formatResendRatio(reply, getNumResentDirect(), getNumSentDirect());
}

void MyMesh::formatFloodSuppressRatioReply(char *reply) {
  if (!_prefs.flood_suppress) return;  // plain "> off" when the master switch is off
  StatsFormatHelper::formatFloodSuppressRatio(reply, _fs_suppressed, _fs_seen);
}

// `clients` reply: one line per attached leaf client "<hash>:<age>s" -- the hash is
// the learned identity prefix (8-hex when seeded from an advert, 2-hex when seeded
// only from a message src_hash), `:` age in seconds + `s`. Newline-separated,
// "-none-" if empty. Byte-minimal -- this text travels over LoRa as the REQ->RESPONSE
// payload. Mirrors formatNeighborsReply (same 134-byte guard).
void MyMesh::formatClientsReply(char *reply) {
  char *dp = reply;
  uint32_t now = getRTCClock()->getCurrentTime();
  for (int i = 0; i < MAX_ATTACHED_CLIENTS && dp - reply < 134; i++) {
    if (!_attached[i].active) continue;
    if (dp != reply) *dp++ = '\n';
    char hex[9];
    mesh::Utils::toHex(hex, _attached[i].prefix, _attached[i].prefix_len);
    uint32_t secs = now - _attached[i].last_seen;
    sprintf(dp, "%s:%us", hex, (unsigned)secs);
    while (*dp) dp++;
  }
  if (dp == reply) strcpy(reply, "-none-");
}

// `reach <hash>` reply: directed reach edges of one NEAR repeater, as two lines:
//   line 1 '<' + reached-by  (incoming: near neighbours that reach this node)
//   line 2 '>' + reaches     (outgoing: near neighbours this node reaches)
// Endpoints are 4-byte/8-hex prefixes resolved from the neighbour table (so they
// cross-reference `neighbors`); '-' marks an empty list. Byte-minimal (LoRa).
// Status words for the non-near cases: notnear / unknown / ambig.
void MyMesh::formatReachReply(char *reply, const uint8_t* hash, uint8_t hash_len) {
#if MAX_NEIGHBOURS
  uint32_t now = getRTCClock()->getCurrentTime();
  int8_t me = -1; int near_matches = 0, known_matches = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (neighbours[i].heard_timestamp == 0) continue;
    if (neighbours[i].id.isHashMatch(hash, hash_len)) {
      known_matches++;
      if (isNearNeighbour(i, now)) { near_matches++; me = i; }
    }
  }
  if (near_matches == 0) { strcpy(reply, known_matches == 0 ? "unknown" : "notnear"); return; }
  if (near_matches > 1) { strcpy(reply, "ambig"); return; }

  uint8_t hs = TRACE_MEAS_HASH_SIZE;           // reach edges are measured at the TRACE hash width
  char *dp = reply;
  *dp++ = '<';                                  // line 1: reached-by (j -> me)
  int n = 0;
  for (int j = 0; j < MAX_NEIGHBOURS; j++) {
    if (j == me || !isNearNeighbour(j, now) || !nearReaches(j, me, hs)) continue;
    if (dp - reply > 138) { strcpy(dp, "..."); dp += 3; break; }   // overflow guard
    if (n > 0) *dp++ = ',';
    char hex[9]; mesh::Utils::toHex(hex, neighbours[j].id.pub_key, 4);
    for (const char *s = hex; *s; ) *dp++ = *s++;
    n++;
  }
  if (n == 0) *dp++ = '-';
  *dp++ = '\n';
  *dp++ = '>';                                  // line 2: reaches (me -> j)
  n = 0;
  for (int j = 0; j < MAX_NEIGHBOURS; j++) {
    if (j == me || !isNearNeighbour(j, now) || !nearReaches(me, j, hs)) continue;
    if (dp - reply > 150) { strcpy(dp, "..."); dp += 3; break; }
    if (n > 0) *dp++ = ',';
    char hex[9]; mesh::Utils::toHex(hex, neighbours[j].id.pub_key, 4);
    for (const char *s = hex; *s; ) *dp++ = *s++;
    n++;
  }
  if (n == 0) *dp++ = '-';
  *dp = 0;
#else
  strcpy(reply, "unknown");
#endif
}

// `near` reply: the near coverage peers (fresh + SNR>=snr_lo), strongest first -- the
// exact set the coverage test / TRACE measurement acts on. The header carries the active
// snr_lo threshold and the coverage cap, so the cutoff is visible. Entries beyond
// NEAR_NEIGHBOUR_COVERAGE_CAP are marked '~' (near but NOT owed coverage -- only the
// capped strongest set is guaranteed/TRACE-measured). HASH:secs_ago:snr mirrors
// formatNeighborsReply (snr is x4). Byte budget like formatNeighborsReply (~150 ceiling).
void MyMesh::formatNearReply(char *reply) {
#if MAX_NEIGHBOURS
  char *dp = reply;
  uint32_t now = getRTCClock()->getCurrentTime();

  // collect near-neighbour indices, then insertion-sort by SNR desc (stable on ties)
  int8_t idx[MAX_NEIGHBOURS];
  uint8_t n = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    if (isNearNeighbour(i, now)) idx[n++] = (int8_t)i;
  }
  for (uint8_t a = 1; a < n; a++) {
    int8_t v = idx[a]; int8_t vs = neighbours[v].snr; uint8_t b = a;
    while (b > 0 && neighbours[idx[b - 1]].snr < vs) { idx[b] = idx[b - 1]; b--; }
    idx[b] = v;
  }

  sprintf(dp, "near snr_lo=%d cap=%d n=%u", (int)_prefs.flood_suppress_snr_lo,
          (int)NEAR_NEIGHBOUR_COVERAGE_CAP, (unsigned)n);
  while (*dp) dp++;

  // coverage-TRACE health: sent=attempts, ret=round-trips that came back, edge=links
  // recorded (ret with SNR>=snr_lo), tmo=pairs that timed out twice (no link), neg=pairs cached
  // as no-edge (timeout or weak return) and skipped on a per-pair exponential backoff (capped
  // ~10h; a transient failure retries within ~2 min, a permanent one ramps to ~10h). If sent>0
  // but ret==0 the round trips never complete (loss/collisions); if ret>0 but edge==0 the
  // measured inter-neighbour links are below snr_lo; if sent==0 no top-N>=2 window yet.
  // harv=edges/negatives adopted from overheard neighbours' TRACES (Part 2); unr=near neighbours
  // M cannot transmit-reach and so excludes from the protection set (Part 3).
  uint8_t unr = 0;
  for (int i = 0; i < MAX_NEIGHBOURS; i++)
    if (isNearNeighbour(i, now) && isExcludedFromProtection(i, millis())) unr++;
  sprintf(dp, "\nmeas sent=%lu ret=%lu edge=%lu tmo=%lu neg=%lu harv=%lu unr=%u",
          (unsigned long)_meas_sent, (unsigned long)_meas_returned,
          (unsigned long)_meas_edge, (unsigned long)_meas_timeout, (unsigned long)_meas_neg,
          (unsigned long)_meas_harvested, (unsigned)unr);
  while (*dp) dp++;

  // 150-byte ceiling minus a worst-case entry (~26B: \n + ~ + 8hex + :secs:snr)
  for (uint8_t k = 0; k < n && dp - reply < 150 - 26; k++) {
    *dp++ = '\n';
    if (k >= NEAR_NEIGHBOUR_COVERAGE_CAP) *dp++ = '~';   // near but beyond the coverage cap
    char hex[10];
    mesh::Utils::toHex(hex, neighbours[idx[k]].id.pub_key, 4);
    uint32_t secs_ago = now - neighbours[idx[k]].heard_timestamp;
    sprintf(dp, "%s:%d:%d", hex, (int)secs_ago, (int)neighbours[idx[k]].snr);
    while (*dp) dp++;
  }
  if (n == 0) { *dp++ = '\n'; strcpy(dp, "-none-"); while (*dp) dp++; }
  *dp = 0;
#else
  strcpy(reply, "near: disabled");
#endif
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
  _fs_seen = 0;
  _fs_suppressed = 0;
  _meas_sent = _meas_returned = _meas_edge = _meas_timeout = _meas_neg = 0;
  _meas_harvested = _meas_harvest_neg = 0;
}

static char* trimSpaces(char* s) {
  while (*s == ' ') s++;
  char* end = s + strlen(s);
  while (end > s && end[-1] == ' ') end--;
  *end = 0;
  return s;
}

static bool parsePathCommand(char* raw, uint8_t* out_path, uint8_t& out_path_len, const char*& err) {
  if (raw == NULL || out_path == NULL) {
    err = "Err - bad params";
    return false;
  }

  char* spec = trimSpaces(raw);
  if (*spec == 0) {
    err = "Err - missing path";
    return false;
  }
  if (strcmp(spec, "clear") == 0 || strcmp(spec, "-") == 0 || strcmp(spec, "none") == 0) {
    out_path_len = OUT_PATH_UNKNOWN;
    return true;
  }
  if (strcmp(spec, "flood") == 0) {
    out_path_len = OUT_PATH_FORCE_FLOOD;
    return true;
  }
  if (strcmp(spec, "direct") == 0) {
    out_path_len = 0;
    return true;
  }

  uint8_t hash_size = 0;
  uint8_t hop_count = 0;
  char* token = spec;
  while (token && *token) {
    char* comma = strchr(token, ',');
    if (comma) *comma = 0;
    token = trimSpaces(token);

    int hex_len = strlen(token);
    if (!(hex_len == 2 || hex_len == 4 || hex_len == 6)) {
      err = "Err - bad params";
      return false;
    }

    uint8_t hop_hash_size = (uint8_t)(hex_len / 2);
    if (hash_size == 0) {
      hash_size = hop_hash_size;
    } else if (hash_size != hop_hash_size) {
      err = "Err - bad params";
      return false;
    }

    if (hop_count >= 63 || (hop_count + 1) * hash_size > MAX_PATH_SIZE) {
      err = "Err - bad params";
      return false;
    }
    if (!mesh::Utils::fromHex(&out_path[hop_count * hash_size], hash_size, token)) {
      err = "Err - bad hex";
      return false;
    }

    hop_count++;
    token = comma ? comma + 1 : NULL;
  }

  if (hash_size == 0 || hop_count == 0) {
    err = "Err - missing path";
    return false;
  }
  out_path_len = ((hash_size - 1) << 6) | (hop_count & 63);
  return true;
}

static void formatPathReply(const uint8_t* path, uint8_t path_len, char* out, size_t out_len) {
  if (path_len == OUT_PATH_FORCE_FLOOD) {
    snprintf(out, out_len, "> flood");
    return;
  }
  if (path_len == OUT_PATH_UNKNOWN) {
    snprintf(out, out_len, "> unknown");
    return;
  }
  if (!mesh::Packet::isValidPathLen(path_len)) {
    snprintf(out, out_len, "> invalid");
    return;
  }
  if ((path_len & 63) == 0) {
    snprintf(out, out_len, "> direct");
    return;
  }

  uint8_t hash_size = (path_len >> 6) + 1;
  uint8_t hop_count = path_len & 63;
  uint8_t byte_len = hop_count * hash_size;
  char hex[(MAX_PATH_SIZE * 2) + 1];
  mesh::Utils::toHex(hex, path, byte_len);
  snprintf(out, out_len, "> hs=%u hops=%u hex=%s", (uint32_t)hash_size, (uint32_t)hop_count, hex);
}

void MyMesh::handleCommand(uint32_t sender_timestamp, ClientInfo* sender, char *command, char *reply) {
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    command += 3;
  }

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
  } else if (strcmp(command, "get outpath") == 0
          || strcmp(command, "set outpath") == 0
          || strncmp(command, "set outpath ", 12) == 0) {
    bool is_get = strncmp(command, "get ", 4) == 0;
    if (sender == NULL) {
      strcpy(reply, "Err - command needs remote client context");
    } else if (is_get) {
      formatPathReply(sender->out_path, sender->out_path_len, reply, 160);
    } else {
      char* spec = command + 11;  // length of "set outpath"
      if (*spec == ' ') spec++;

      uint8_t path[MAX_PATH_SIZE];
      uint8_t path_len = OUT_PATH_UNKNOWN;
      const char* err = NULL;
      if (!parsePathCommand(spec, path, path_len, err)) {
        strcpy(reply, err ? err : "Err - invalid path");
      } else {
        if (path_len == OUT_PATH_UNKNOWN || path_len == OUT_PATH_FORCE_FLOOD) {
          memset(sender->out_path, 0, sizeof(sender->out_path));
          sender->out_path_len = path_len;
        } else {
          sender->out_path_len = mesh::Packet::copyPath(sender->out_path, path, path_len);
        }
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
        formatPathReply(sender->out_path, sender->out_path_len, reply, 160);
      }
    }
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

void MyMesh::loop() {
#ifdef WITH_BRIDGE
  bridge.loop();
#endif

  mesh::Mesh::loop();

  _flood_supp.purge(millis());   // evict stale flood-suppression entries
  _nbr_links.purge(millis());    // evict stale inter-neighbour reach edges (~36h TTL)
  _nbr_links.purgeNegative(millis());  // evict expired no-edge cache entries (~10h TTL)
  purgeAttachedClients(getRTCClock()->getCurrentTime());  // evict stale attached-client entries (~24h)

  stepCoverageMeasurement();     // actively probe (TRACE) coverage among top-N near neighbours

  if (_prefs.flood_suppress && millisHasNowPassed(_fs_next_recompute_ms)) {
    updateAdaptiveFloodParams();              // derive _fs_eff_c/_fs_eff_hi from neighbour table
    _fs_next_recompute_ms = futureMillis(60UL * 1000);  // every 1 min (reaction latency; cost is negligible)
  }

  if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    uint32_t delay_millis = 0;
    if (pkt) sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_driver.setParams(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    acl.save(_fs);
    dirty_contacts_expiry = 0;
  }

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
#if defined(WITH_BRIDGE)
  if (bridge.isRunning()) return true;  // bridge needs WiFi radio, can't sleep
#endif
  return _mgr->getOutboundTotal() > 0;
}
