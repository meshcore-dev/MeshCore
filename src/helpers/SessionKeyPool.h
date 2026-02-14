#pragma once

#include <MeshCore.h>
#include <string.h>

#define SESSION_STATE_NONE        0
#define SESSION_STATE_INIT_SENT   1  // initiator: INIT sent, waiting for ACCEPT
#define SESSION_STATE_DUAL_DECODE 2  // responder: new key active, old key still valid
#define SESSION_STATE_ACTIVE      3  // session key confirmed and in use

#define SESSION_FLAG_PREV_VALID   0x01  // prev_session_key is valid for dual-decode

struct SessionKeyEntry {
  uint8_t peer_pub_prefix[4];       // first 4 bytes of peer's public key
  uint8_t session_key[SESSION_KEY_SIZE];
  uint8_t prev_session_key[SESSION_KEY_SIZE];
  uint16_t nonce;                    // session key nonce counter (starts at 1)
  uint8_t state;                     // SESSION_STATE_*
  uint8_t sends_since_last_recv;     // RAM-only counter, threshold SESSION_KEY_STALE_THRESHOLD
  uint8_t retries_left;              // remaining INIT retries this round
  unsigned long timeout_at;          // millis timestamp for INIT timeout
  uint8_t ephemeral_prv[PRV_KEY_SIZE]; // initiator-only: ephemeral private key (zeroed after use)
  uint8_t ephemeral_pub[PUB_KEY_SIZE]; // initiator-only: ephemeral public key
};

class SessionKeyPool {
  SessionKeyEntry entries[MAX_SESSION_KEYS];
  int count;

public:
  SessionKeyPool() : count(0) {
    memset(entries, 0, sizeof(entries));
  }

  SessionKeyEntry* findByPrefix(const uint8_t* pub_key) {
    for (int i = 0; i < count; i++) {
      if (memcmp(entries[i].peer_pub_prefix, pub_key, 4) == 0) {
        return &entries[i];
      }
    }
    return nullptr;
  }

  SessionKeyEntry* allocate(const uint8_t* pub_key) {
    // Check if already exists
    auto existing = findByPrefix(pub_key);
    if (existing) return existing;

    // Find free slot or evict oldest
    if (count < MAX_SESSION_KEYS) {
      auto e = &entries[count++];
      memset(e, 0, sizeof(*e));
      memcpy(e->peer_pub_prefix, pub_key, 4);
      return e;
    }
    // Pool full — evict the entry with state NONE, or the first one
    for (int i = 0; i < MAX_SESSION_KEYS; i++) {
      if (entries[i].state == SESSION_STATE_NONE) {
        memset(&entries[i], 0, sizeof(entries[i]));
        memcpy(entries[i].peer_pub_prefix, pub_key, 4);
        return &entries[i];
      }
    }
    // All slots active — evict first entry
    memset(&entries[0], 0, sizeof(entries[0]));
    memcpy(entries[0].peer_pub_prefix, pub_key, 4);
    return &entries[0];
  }

  void remove(const uint8_t* pub_key) {
    for (int i = 0; i < count; i++) {
      if (memcmp(entries[i].peer_pub_prefix, pub_key, 4) == 0) {
        // Shift remaining entries down
        count--;
        for (int j = i; j < count; j++) {
          entries[j] = entries[j + 1];
        }
        memset(&entries[count], 0, sizeof(entries[count]));
        return;
      }
    }
  }

  int getCount() const { return count; }
  SessionKeyEntry* getByIdx(int idx) { return (idx >= 0 && idx < count) ? &entries[idx] : nullptr; }

  // Persistence helpers — 71-byte records: [pub_prefix:4][flags:1][nonce:2][session_key:32][prev_session_key:32]
  // Returns false when idx is past end
  bool getEntryForSave(int idx, uint8_t* pub_key_prefix, uint8_t* flags, uint16_t* nonce,
                       uint8_t* session_key, uint8_t* prev_session_key) {
    if (idx >= count) return false;
    auto& e = entries[idx];
    if (e.state == SESSION_STATE_NONE || e.state == SESSION_STATE_INIT_SENT) return false; // don't persist pending negotiations
    memcpy(pub_key_prefix, e.peer_pub_prefix, 4);
    *flags = (e.state == SESSION_STATE_DUAL_DECODE) ? SESSION_FLAG_PREV_VALID : 0;
    *nonce = e.nonce;
    memcpy(session_key, e.session_key, SESSION_KEY_SIZE);
    memcpy(prev_session_key, e.prev_session_key, SESSION_KEY_SIZE);
    return true;
  }

  bool applyLoaded(const uint8_t* pub_key_prefix, uint8_t flags, uint16_t nonce,
                   const uint8_t* session_key, const uint8_t* prev_session_key) {
    auto e = allocate(pub_key_prefix);
    if (!e) return false;
    e->nonce = nonce;
    e->state = (flags & SESSION_FLAG_PREV_VALID) ? SESSION_STATE_DUAL_DECODE : SESSION_STATE_ACTIVE;
    e->sends_since_last_recv = 0;
    e->retries_left = 0;
    e->timeout_at = 0;
    memcpy(e->session_key, session_key, SESSION_KEY_SIZE);
    memcpy(e->prev_session_key, prev_session_key, SESSION_KEY_SIZE);
    memset(e->ephemeral_prv, 0, sizeof(e->ephemeral_prv));
    memset(e->ephemeral_pub, 0, sizeof(e->ephemeral_pub));
    return true;
  }
};
