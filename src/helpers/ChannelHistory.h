#pragma once

#include <stdint.h>
#include <string.h>

// Hardware-independent store-and-forward cache of recent public-channel (GRP_TXT)
// broadcasts. A repeater already relays every channel packet that passes through
// it; this keeps a bounded copy so a companion that was out of range can pull back
// what it missed.
//
// The repeater holds no channel key, so packets are stored - and later replayed -
// still encrypted; the requesting client decrypts them with its own channel key.
// A cached entry's raw[0] is the GRP_TXT channel-hash prefix.
//
// Kept free of Arduino / radio dependencies so it can be unit-tested natively
// (see test/test_channel_history).
//
// NUM_ENTRIES : ring-buffer depth (oldest entry evicted when full)
// MAX_RAW     : largest packet payload cached; must be small enough that one entry
//               fits inside a single response packet (see serve()).
template <int NUM_ENTRIES, int MAX_RAW>
class ChannelHistory {
public:
  // per-served-record header: recv_ts(4) + raw_len(1)
  static const int REC_HEADER = 5;

  ChannelHistory() { clear(); }

  void clear() {
    memset(_entries, 0, sizeof(_entries));
    _next_idx = 0;
  }

  // Cache one public-channel payload (caller has already checked it is GRP_TXT).
  // Returns true if newly stored, false if skipped: empty, too large to ever replay
  // in one response, or a duplicate re-flood already held.
  //
  // Dedup is by content: a GRP_TXT payload embeds the sender's encrypted timestamp,
  // so identical bytes mean the very same packet; distinct messages never collide.
  bool capture(const uint8_t* payload, uint16_t payload_len, uint32_t now_ts) {
    if (payload_len < 1 || payload_len > MAX_RAW) return false;

    for (int k = 0; k < NUM_ENTRIES; k++) {
      const Entry& e = _entries[k];
      if (e.recv_timestamp != 0 && e.raw_len == payload_len
          && memcmp(e.raw, payload, payload_len) == 0) {
        return false;   // already cached
      }
    }

    Entry& slot = _entries[_next_idx];
    slot.recv_timestamp = now_ts;
    slot.raw_len = (uint8_t)payload_len;
    memcpy(slot.raw, payload, payload_len);
    _next_idx = (_next_idx + 1) % NUM_ENTRIES;
    return true;
  }

  // Pack matching cached packets into out[], oldest -> newest, for entries strictly
  // newer than since_ts, up to max_count (0 == no explicit cap), stopping early if
  // the next record would not fit in out_cap. Each record is:
  //   [recv_ts(4)][raw_len(1)][raw ...]
  // The client pages by re-requesting with since_ts = the newest recv_ts it received.
  // Writes the number of records emitted to out_count; returns bytes written.
  int serve(uint8_t want_hash, uint32_t since_ts, uint8_t max_count,
            uint8_t* out, int out_cap, uint8_t& out_count) const {
    if (max_count == 0) max_count = 255;
    uint8_t count = 0;
    int ofs = 0;
    // _next_idx points at the oldest slot once the ring has wrapped, so walking
    // forward from it yields oldest -> newest; empty slots (recv_timestamp==0) skip.
    for (int k = 0; k < NUM_ENTRIES && count < max_count; k++) {
      const Entry& e = _entries[(_next_idx + k) % NUM_ENTRIES];
      if (e.recv_timestamp == 0 || e.recv_timestamp <= since_ts) continue;
      if (want_hash != 0xFF && e.raw[0] != want_hash) continue;   // channel filter
      if (ofs + REC_HEADER + e.raw_len > out_cap) break;          // full; client pages for more
      memcpy(&out[ofs], &e.recv_timestamp, 4); ofs += 4;
      out[ofs++] = e.raw_len;
      memcpy(&out[ofs], e.raw, e.raw_len); ofs += e.raw_len;
      count++;
    }
    out_count = count;
    return ofs;
  }

  // Number of non-empty entries currently held (for diagnostics/debug dumps).
  int countStored() const {
    int n = 0;
    for (int k = 0; k < NUM_ENTRIES; k++) {
      if (_entries[k].recv_timestamp != 0) n++;
    }
    return n;
  }

  // Read the i-th non-empty entry oldest -> newest (for diagnostics). Returns false
  // past the end. channel_hash / raw_len / recv_ts are optional out-params.
  bool entryAt(int i, uint8_t* channel_hash, uint8_t* raw_len, uint32_t* recv_ts) const {
    int seen = 0;
    for (int k = 0; k < NUM_ENTRIES; k++) {
      const Entry& e = _entries[(_next_idx + k) % NUM_ENTRIES];
      if (e.recv_timestamp == 0) continue;
      if (seen == i) {
        if (channel_hash) *channel_hash = e.raw[0];
        if (raw_len) *raw_len = e.raw_len;
        if (recv_ts) *recv_ts = e.recv_timestamp;
        return true;
      }
      seen++;
    }
    return false;
  }

private:
  struct Entry {
    uint32_t recv_timestamp;      // 0 == empty slot
    uint8_t  raw_len;
    uint8_t  raw[MAX_RAW];        // GRP_TXT payload, verbatim (still encrypted); raw[0] = channel hash
  };
  Entry   _entries[NUM_ENTRIES];
  uint8_t _next_idx;
};
