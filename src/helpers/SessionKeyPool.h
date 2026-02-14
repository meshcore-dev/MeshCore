#pragma once

#include <MeshCore.h>
#include <string.h>

#define SESSION_STATE_NONE        0
#define SESSION_STATE_INIT_SENT   1  // initiator: INIT sent, waiting for ACCEPT
#define SESSION_STATE_DUAL_DECODE 2  // responder: new key active, old key still valid
#define SESSION_STATE_ACTIVE      3  // session key confirmed and in use


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
  uint32_t last_used;                // LRU counter (higher = more recent)
};

class SessionKeyPool {
  SessionKeyEntry entries[MAX_SESSION_KEYS_RAM];
  int count;
  uint32_t lru_counter;

  // Track prefixes removed since last save, so merge-save doesn't resurrect them
  uint8_t removed_prefixes[MAX_SESSION_KEYS_RAM][4];
  int removed_count;

  void touch(SessionKeyEntry* entry) {
    entry->last_used = ++lru_counter;
  }

public:
  SessionKeyPool() : count(0), lru_counter(0), removed_count(0) {
    memset(entries, 0, sizeof(entries));
    memset(removed_prefixes, 0, sizeof(removed_prefixes));
  }

  bool isFull() const { return count >= MAX_SESSION_KEYS_RAM; }

  SessionKeyEntry* findByPrefix(const uint8_t* pub_key) {
    for (int i = 0; i < count; i++) {
      if (memcmp(entries[i].peer_pub_prefix, pub_key, 4) == 0) {
        touch(&entries[i]);
        return &entries[i];
      }
    }
    return nullptr;
  }

  // Lookup without updating LRU — use during save/merge to avoid perturbing eviction order
  bool hasPrefix(const uint8_t* pub_key) const {
    for (int i = 0; i < count; i++) {
      if (memcmp(entries[i].peer_pub_prefix, pub_key, 4) == 0) return true;
    }
    return false;
  }

  SessionKeyEntry* allocate(const uint8_t* pub_key) {
    // Check if already exists
    auto existing = findByPrefix(pub_key);
    if (existing) return existing;

    // Find free slot
    if (count < MAX_SESSION_KEYS_RAM) {
      auto e = &entries[count++];
      memset(e, 0, sizeof(*e));
      memcpy(e->peer_pub_prefix, pub_key, 4);
      touch(e);
      return e;
    }
    // Pool full — LRU eviction, skip INIT_SENT entries (ephemeral keys are RAM-only)
    int evict_idx = -1;
    uint32_t min_used = 0xFFFFFFFF;
    for (int i = 0; i < MAX_SESSION_KEYS_RAM; i++) {
      if (entries[i].state == SESSION_STATE_INIT_SENT) continue;
      if (entries[i].last_used < min_used) {
        min_used = entries[i].last_used;
        evict_idx = i;
      }
    }
    if (evict_idx < 0) evict_idx = 0;  // all INIT_SENT — shouldn't happen, fall back to [0]
    memset(&entries[evict_idx], 0, sizeof(entries[evict_idx]));
    memcpy(entries[evict_idx].peer_pub_prefix, pub_key, 4);
    touch(&entries[evict_idx]);
    return &entries[evict_idx];
  }

  void remove(const uint8_t* pub_key) {
    for (int i = 0; i < count; i++) {
      if (memcmp(entries[i].peer_pub_prefix, pub_key, 4) == 0) {
        // Track removed prefix for merge-save
        if (removed_count < MAX_SESSION_KEYS_RAM) {
          memcpy(removed_prefixes[removed_count++], entries[i].peer_pub_prefix, 4);
        }
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

  bool isRemoved(const uint8_t* pub_key_prefix) const {
    for (int i = 0; i < removed_count; i++) {
      if (memcmp(removed_prefixes[i], pub_key_prefix, 4) == 0) return true;
    }
    return false;
  }

  void clearRemoved() { removed_count = 0; }

  int getCount() const { return count; }
  SessionKeyEntry* getByIdx(int idx) { return (idx >= 0 && idx < count) ? &entries[idx] : nullptr; }

  // Persistence helpers — variable-length records: [pub_prefix:4][flags:1][nonce:2][session_key:32][prev_session_key:32 if flags & PREV_VALID]
  // Returns false when idx is past end or entry is not persistable
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
