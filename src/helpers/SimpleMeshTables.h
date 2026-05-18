#pragma once

#include <Mesh.h>
#if ARDUINO
  #include <Arduino.h>
#endif

#ifdef ESP32
  #include <FS.h>
#endif

#define MAX_PACKET_HASHES  128
#define MAX_PACKET_ACKS     64
#ifndef MAX_RECENT_REPEATERS
  // Platform defaults. Can be overridden with -D MAX_RECENT_REPEATERS=<n>.
  #if defined(ESP32) || defined(ESP32_PLATFORM)
    #define MAX_RECENT_REPEATERS  2048
  #elif defined(NRF52_PLATFORM)
    #define MAX_RECENT_REPEATERS  512
  #else
    #define MAX_RECENT_REPEATERS  64
  #endif
#endif
#define MAX_ROUTE_HASH_BYTES   3

class SimpleMeshTables : public mesh::MeshTables {
public:
  typedef bool (*RecentRepeaterAllowFn)(const uint8_t* prefix, uint8_t prefix_len, void* ctx);

  struct RecentRepeaterInfo {
    // Identity and link quality for a next-hop path prefix.
    uint8_t prefix[MAX_ROUTE_HASH_BYTES];
    uint8_t prefix_len;
    int8_t snr_x4;
    uint8_t snr_locked;
    uint32_t last_heard_millis;
  };

private:
  uint8_t _hashes[MAX_PACKET_HASHES*MAX_HASH_SIZE];
  int _next_idx;
  uint32_t _acks[MAX_PACKET_ACKS];
  int _next_ack_idx;
  uint32_t _direct_dups, _flood_dups;
  RecentRepeaterInfo _recent_repeaters[MAX_RECENT_REPEATERS];
  int _next_recent_repeater_idx;
  int8_t _recent_repeater_min_snr_x4;
  RecentRepeaterAllowFn _recent_repeater_allow_fn;
  void* _recent_repeater_allow_ctx;

  bool hasSeenAck(uint32_t ack) const {
    for (int i = 0; i < MAX_PACKET_ACKS; i++) {
      if (ack == _acks[i]) {
        return true;
      }
    }
    return false;
  }

  void storeAck(uint32_t ack) {
    _acks[_next_ack_idx] = ack;
    _next_ack_idx = (_next_ack_idx + 1) % MAX_PACKET_ACKS;
  }

  bool hasSeenHash(const uint8_t* hash) const {
    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0) {
        return true;
      }
    }
    return false;
  }

  void storeHash(const uint8_t* hash) {
    memcpy(&_hashes[_next_idx*MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
    _next_idx = (_next_idx + 1) % MAX_PACKET_HASHES;
  }

  bool prefixesOverlap(const uint8_t* a, uint8_t a_len, const uint8_t* b, uint8_t b_len) const {
    uint8_t n = a_len < b_len ? a_len : b_len;
    return n > 0 && memcmp(a, b, n) == 0;
  }

  int8_t weightedSnrX4RoundUp(int8_t curr_snr_x4, int8_t new_snr_x4) const {
    // Keep existing SNR heavier than a single new sample: 75% existing + 25% new.
    int32_t weighted_sum = ((int32_t)curr_snr_x4 * 3) + (int32_t)new_snr_x4;
    int32_t blended = weighted_sum / 4;  // truncates toward zero
    // "Round up" means ceil(), which only differs from truncation for positive remainders.
    if (weighted_sum > 0 && (weighted_sum % 4) != 0) {
      blended++;
    }
    if (blended > 127) {
      blended = 127;
    } else if (blended < -128) {
      blended = -128;
    }
    return (int8_t)blended;
  }

  bool extractRecentRepeater(const mesh::Packet* packet, uint8_t* prefix, uint8_t& prefix_len) const {
    // Learn repeater prefixes only from packet shapes that expose a trustworthy repeater ID.
    // For flood traffic, the last path entry is the repeater we directly heard.
    if (packet->isRouteFlood() && packet->getPathHashCount() > 0) {
      prefix_len = packet->getPathHashSize();
      if (prefix_len > MAX_ROUTE_HASH_BYTES) {
        prefix_len = MAX_ROUTE_HASH_BYTES;
      }

      const uint8_t* last_hop = &packet->path[(packet->getPathHashCount() - 1) * packet->getPathHashSize()];
      memcpy(prefix, last_hop, prefix_len);
      return true;
    }

    // If there is no flood path to inspect, fall back to payload-derived identities.
    if (packet->getPayloadType() == PAYLOAD_TYPE_ADVERT && packet->payload_len >= PUB_KEY_SIZE) {
      memcpy(prefix, packet->payload, MAX_ROUTE_HASH_BYTES);
      prefix_len = MAX_ROUTE_HASH_BYTES;
      return true;
    }

    if (packet->getPayloadType() == PAYLOAD_TYPE_CONTROL
        && packet->isRouteDirect()
        && packet->getPathHashCount() == 0
        && packet->payload_len >= 6 + MAX_ROUTE_HASH_BYTES
        && (packet->payload[0] & 0xF0) == 0x90) {
      memcpy(prefix, &packet->payload[6], MAX_ROUTE_HASH_BYTES);
      prefix_len = MAX_ROUTE_HASH_BYTES;
      return true;
    }

    return false;
  }

  void recordRecentRepeater(const mesh::Packet* packet) {
    uint8_t prefix[MAX_ROUTE_HASH_BYTES] = {0};
    uint8_t prefix_len = 0;
    if (!extractRecentRepeater(packet, prefix, prefix_len) || prefix_len == 0) {
      return;
    }
    if (packet->_snr < _recent_repeater_min_snr_x4) {
      return;
    }
    setRecentRepeater(prefix, prefix_len, packet->_snr);
  }

public:
  SimpleMeshTables() { 
    memset(_hashes, 0, sizeof(_hashes));
    _next_idx = 0;
    memset(_acks, 0, sizeof(_acks));
    _next_ack_idx = 0;
    _direct_dups = _flood_dups = 0;
    memset(_recent_repeaters, 0, sizeof(_recent_repeaters));
    _next_recent_repeater_idx = 0;
    _recent_repeater_min_snr_x4 = -128;
    _recent_repeater_allow_fn = NULL;
    _recent_repeater_allow_ctx = NULL;
  }

#ifdef ESP32
  void restoreFrom(File f) {
    f.read(_hashes, sizeof(_hashes));
    f.read((uint8_t *) &_next_idx, sizeof(_next_idx));
    f.read((uint8_t *) &_acks[0], sizeof(_acks));
    f.read((uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
    // Recent repeater entries are intentionally not restored across boots.
    // This avoids struct-layout migration issues and keeps stale path quality
    // stats from persisting indefinitely.
    memset(_recent_repeaters, 0, sizeof(_recent_repeaters));
    _next_recent_repeater_idx = 0;
  }
  void saveTo(File f) {
    f.write(_hashes, sizeof(_hashes));
    f.write((const uint8_t *) &_next_idx, sizeof(_next_idx));
    f.write((const uint8_t *) &_acks[0], sizeof(_acks));
    f.write((const uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
  }
#endif

  bool hasSeen(const mesh::Packet* packet) override {
    if (packet->getPayloadType() == PAYLOAD_TYPE_ACK) {
      recordRecentRepeater(packet);

      uint32_t ack;
      memcpy(&ack, packet->payload, 4);

      if (hasSeenAck(ack)) {
        if (packet->isRouteDirect()) {
          _direct_dups++;   // keep some stats
        } else {
          _flood_dups++;
        }
        return true;
      }

      storeAck(ack);
      return false;
    }

    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    if (hasSeenHash(hash)) {
      if (packet->isRouteDirect()) {
        _direct_dups++;   // keep some stats
      } else {
        _flood_dups++;
      }
      return true;
    }

    storeHash(hash);
    recordRecentRepeater(packet);
    return false;
  }

  void markSent(const mesh::Packet* packet) override {
    // Outbound packets must be marked as already-sent without teaching the recent-heard cache about ourselves.
    if (packet->getPayloadType() == PAYLOAD_TYPE_ACK) {
      uint32_t ack;
      memcpy(&ack, packet->payload, 4);
      if (!hasSeenAck(ack)) {
        storeAck(ack);
      }
      return;
    }

    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);
    if (!hasSeenHash(hash)) {
      storeHash(hash);
    }
  }

  void clear(const mesh::Packet* packet) override {
    if (packet->getPayloadType() == PAYLOAD_TYPE_ACK) {
      uint32_t ack;
      memcpy(&ack, packet->payload, 4);
      for (int i = 0; i < MAX_PACKET_ACKS; i++) {
        if (ack == _acks[i]) { 
          _acks[i] = 0;
          break;
        }
      }
    } else {
      uint8_t hash[MAX_HASH_SIZE];
      packet->calculatePacketHash(hash);

      uint8_t* sp = _hashes;
      for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
        if (memcmp(hash, sp, MAX_HASH_SIZE) == 0) { 
          memset(sp, 0, MAX_HASH_SIZE);
          break;
        }
      }
    }
  }

  uint32_t getNumDirectDups() const { return _direct_dups; }
  uint32_t getNumFloodDups() const { return _flood_dups; }

  void setRecentRepeaterMinSNRX4(int8_t min_snr_x4) {
    _recent_repeater_min_snr_x4 = min_snr_x4;
  }
  void setRecentRepeaterAllowFilter(RecentRepeaterAllowFn fn, void* ctx) {
    _recent_repeater_allow_fn = fn;
    _recent_repeater_allow_ctx = ctx;
  }
  bool setRecentRepeater(const uint8_t* prefix, uint8_t prefix_len, int8_t snr_x4, bool snr_locked = false,
                         bool bypass_allow_filter = false) {
    if (prefix == NULL || prefix_len == 0) {
      return false;
    }

    if (prefix_len > MAX_ROUTE_HASH_BYTES) {
      prefix_len = MAX_ROUTE_HASH_BYTES;
    }

    if (!bypass_allow_filter && _recent_repeater_allow_fn != NULL
        && !_recent_repeater_allow_fn(prefix, prefix_len, _recent_repeater_allow_ctx)) {
      return false;
    }

    // Keep one slot for overlapping prefixes so 1/2/3-byte paths share the same entry.
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      RecentRepeaterInfo& existing = _recent_repeaters[i];
      if (existing.prefix_len == 0 || !prefixesOverlap(existing.prefix, existing.prefix_len, prefix, prefix_len)) {
        continue;
      }
      if (prefix_len > existing.prefix_len) {
        memset(existing.prefix, 0, sizeof(existing.prefix));
        memcpy(existing.prefix, prefix, prefix_len);
        existing.prefix_len = prefix_len;
      }
      if (snr_locked) {
        existing.snr_x4 = snr_x4;
        existing.snr_locked = 1;
      } else if (!existing.snr_locked) {
        existing.snr_x4 = weightedSnrX4RoundUp(existing.snr_x4, snr_x4);
      }
#if ARDUINO
      existing.last_heard_millis = millis();
#else
      existing.last_heard_millis = 0;
#endif
      return true;
    }

    int slot_idx = -1;
    // Prefer empty slots first while preserving newest-order iteration.
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      int idx = (_next_recent_repeater_idx + i) % MAX_RECENT_REPEATERS;
      if (_recent_repeaters[idx].prefix_len == 0) {
        slot_idx = idx;
        break;
      }
    }
    if (slot_idx < 0) {
      // Table is full: evict the weakest observed SNR entry.
      slot_idx = 0;
      int8_t min_snr_x4 = _recent_repeaters[0].snr_x4;
      for (int i = 1; i < MAX_RECENT_REPEATERS; i++) {
        if (_recent_repeaters[i].snr_x4 < min_snr_x4) {
          min_snr_x4 = _recent_repeaters[i].snr_x4;
          slot_idx = i;
        }
      }
    }

    RecentRepeaterInfo& slot = _recent_repeaters[slot_idx];
    memset(slot.prefix, 0, sizeof(slot.prefix));
    memcpy(slot.prefix, prefix, prefix_len);
    slot.prefix_len = prefix_len;
    slot.snr_x4 = snr_x4;
    slot.snr_locked = snr_locked ? 1 : 0;
#if ARDUINO
    slot.last_heard_millis = millis();
#else
    slot.last_heard_millis = 0;
#endif
    _next_recent_repeater_idx = (slot_idx + 1) % MAX_RECENT_REPEATERS;
    return true;
  }
  bool decrementRecentRepeaterSnrX4(const uint8_t* prefix, uint8_t prefix_len, uint8_t amount_x4 = 1) {
    if (prefix == NULL || prefix_len == 0 || amount_x4 == 0) {
      return false;
    }
    if (prefix_len > MAX_ROUTE_HASH_BYTES) {
      prefix_len = MAX_ROUTE_HASH_BYTES;
    }

    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      RecentRepeaterInfo& existing = _recent_repeaters[i];
      if (existing.prefix_len == 0 || !prefixesOverlap(existing.prefix, existing.prefix_len, prefix, prefix_len)) {
        continue;
      }
      if (prefix_len > existing.prefix_len) {
        memset(existing.prefix, 0, sizeof(existing.prefix));
        memcpy(existing.prefix, prefix, prefix_len);
        existing.prefix_len = prefix_len;
      }
      if (!existing.snr_locked) {
        int16_t lowered = (int16_t)existing.snr_x4 - (int16_t)amount_x4;
        if (lowered < -128) {
          lowered = -128;
        }
        existing.snr_x4 = (int8_t)lowered;
      }
      return true;
    }
    return false;
  }
  const RecentRepeaterInfo* getLatestRecentRepeater() const {
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      int idx = (_next_recent_repeater_idx - 1 - i + MAX_RECENT_REPEATERS) % MAX_RECENT_REPEATERS;
      const RecentRepeaterInfo* info = &_recent_repeaters[idx];
      if (info->prefix_len > 0) {
        return info;
      }
    }
    return NULL;
  }
  int getRecentRepeaterCount() const {
    int count = 0;
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      if (_recent_repeaters[i].prefix_len > 0) {
        count++;
      }
    }
    return count;
  }
  const RecentRepeaterInfo* getRecentRepeaterNewestByIdx(int idx_wanted) const {
    if (idx_wanted < 0) {
      return NULL;
    }
    int idx_seen = 0;
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      int idx = (_next_recent_repeater_idx - 1 - i + MAX_RECENT_REPEATERS) % MAX_RECENT_REPEATERS;
      const RecentRepeaterInfo* info = &_recent_repeaters[idx];
      if (info->prefix_len == 0) {
        continue;
      }
      if (idx_seen == idx_wanted) {
        return info;
      }
      idx_seen++;
    }
    return NULL;
  }
  const RecentRepeaterInfo* getRecentRepeaterOldestByIdx(int idx_wanted) const {
    if (idx_wanted < 0) {
      return NULL;
    }
    int idx_seen = 0;
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      int idx = (_next_recent_repeater_idx + i) % MAX_RECENT_REPEATERS;
      const RecentRepeaterInfo* info = &_recent_repeaters[idx];
      if (info->prefix_len == 0) {
        continue;
      }
      if (idx_seen == idx_wanted) {
        return info;
      }
      idx_seen++;
    }
    return NULL;
  }

  const RecentRepeaterInfo* findRecentRepeaterByHash(const uint8_t* hash, uint8_t hash_len) const {
    if (hash == NULL || hash_len == 0) {
      return NULL;
    }

    // Search newest-to-oldest and allow 1/2/3-byte prefixes to overlap-match.
    for (int i = 0; i < MAX_RECENT_REPEATERS; i++) {
      int idx = (_next_recent_repeater_idx - 1 - i + MAX_RECENT_REPEATERS) % MAX_RECENT_REPEATERS;
      const RecentRepeaterInfo* info = &_recent_repeaters[idx];
      if (info->prefix_len == 0) {
        continue;
      }
      if (prefixesOverlap(info->prefix, info->prefix_len, hash, hash_len)) {
        return info;
      }
    }
    return NULL;
  }
  void clearRecentRepeaters() {
    memset(_recent_repeaters, 0, sizeof(_recent_repeaters));
    _next_recent_repeater_idx = 0;
  }

  void resetStats() { _direct_dups = _flood_dups = 0; }
};
