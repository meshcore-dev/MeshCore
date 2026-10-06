#pragma once
#include "FlashRecord.h"

// Two-row A/B store for the 160-byte FlashRecord. Each commit writes the OTHER row with seq+1, so a
// power loss while writing leaves the previous record intact (the SFLASH row erase+program is not atomic).
// commit() changes the in-RAM state only after the backend confirmed the write: a rejected change can
// never leak into a later commit (review I1). Rows are 256 bytes apart (one SFLASH user row each).
//
// Backend concept:
//   void read(size_t offset, uint8_t* buf, size_t n);
//   bool write(size_t offset, const uint8_t* buf, size_t n);   // must persist exactly the rows touched
namespace asr650x {

const size_t ROW_STRIDE = 256;

inline bool seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

template <class Backend>
class PersistStore {
  Backend& _be;
  PersistState _state;
  uint8_t _last[PERSIST_SIZE];     // bytes of the newest valid record (what is in flash now)
  bool _have_last;
  int _next_row;                   // row the next commit writes

public:
  explicit PersistStore(Backend& be) : _be(be), _have_last(false), _next_row(0) {
    memset(&_state, 0, sizeof(_state));
    memset(_last, 0, sizeof(_last));
  }

  const PersistState& state() const { return _state; }

  void begin() {
    uint8_t buf[2][PERSIST_SIZE];
    PersistState st[2];
    bool ok[2];
    for (int r = 0; r < 2; r++) {
      _be.read(r * ROW_STRIDE, buf[r], PERSIST_SIZE);
      ok[r] = persist_decode(buf[r], st[r]);
    }
    int pick = -1;
    if (ok[0] && ok[1]) pick = seq_newer(st[1].seq, st[0].seq) ? 1 : 0;
    else if (ok[0]) pick = 0;
    else if (ok[1]) pick = 1;
    if (pick < 0) {
      memset(&_state, 0, sizeof(_state));
      _have_last = false;
      _next_row = 0;
      return;
    }
    _state = st[pick];
    memcpy(_last, buf[pick], PERSIST_SIZE);
    _have_last = true;
    _next_row = pick ^ 1;
  }

  bool commit(const PersistState& candidate) {
    PersistState c = candidate;
    uint8_t buf[PERSIST_SIZE];
    if (_have_last) {                // unchanged content -> no flash write
      c.seq = _state.seq;
      persist_encode(c, buf);
      if (memcmp(buf, _last, PERSIST_SIZE) == 0) return true;
    }
    c.seq = _state.seq + 1;
    persist_encode(c, buf);
    if (!_be.write(_next_row * ROW_STRIDE, buf, PERSIST_SIZE)) return false;   // state untouched on failure
    _state = c;
    memcpy(_last, buf, PERSIST_SIZE);
    _have_last = true;
    _next_row ^= 1;
    return true;
  }
};

}  // namespace asr650x
