#pragma once

#include <Mesh.h>

#ifdef ESP32
  #include <FS.h>
#endif

#define MAX_PACKET_HASHES  (128+32)

class SimpleMeshTables : public mesh::MeshTables {
  uint8_t _hashes[MAX_PACKET_HASHES*MAX_HASH_SIZE];
  uint32_t _last_seen[MAX_PACKET_HASHES];  // timestamp for LRU eviction
  uint32_t _direct_dups, _flood_dups;

public:
  SimpleMeshTables() {
    memset(_hashes, 0, sizeof(_hashes));
    memset(_last_seen, 0, sizeof(_last_seen));
    _direct_dups = _flood_dups = 0;
  }

#ifdef ESP32
  void restoreFrom(File f) {
    f.read(_hashes, sizeof(_hashes));
    int dummy_idx;
    f.read((uint8_t *) &dummy_idx, sizeof(dummy_idx));  // legacy _next_idx, ignore
    // Treat restored hashes as just seen - give them fresh timestamps
    uint32_t now = millis();
    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      // Check if slot has data (not all zeros)
      bool empty = true;
      for (int j = 0; j < MAX_HASH_SIZE && empty; j++) {
        if (sp[j] != 0) empty = false;
      }
      _last_seen[i] = empty ? 0 : now;
    }
  }
  void saveTo(File f) {
    f.write(_hashes, sizeof(_hashes));
    int dummy_idx = 0;
    f.write((const uint8_t *) &dummy_idx, sizeof(dummy_idx));  // legacy format
  }
#endif

  bool wasSeen(const mesh::Packet* packet) override {
    uint32_t now = millis();
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0 && _last_seen[i] != 0) {
        // Match found - refresh timestamp (LRU touch) and return true
        _last_seen[i] = now;
        if (packet->isRouteDirect()) {
          _direct_dups++;
        } else {
          _flood_dups++;
        }
        return true;
      }
    }
    return false;
  }

  void markSeen(const mesh::Packet* packet) override {
    uint32_t now = millis();
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    // Insert into oldest slot (LRU eviction). Empty slots have _last_seen == 0,
    // hence maximal age, so they get filled before any real entry is evicted.
    int oldest_idx = 0;
    uint32_t oldest_age = 0;
    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      uint32_t age = now - _last_seen[i];
      if (age > oldest_age) {
        oldest_age = age;
        oldest_idx = i;
      }
    }
    memcpy(&_hashes[oldest_idx*MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
    _last_seen[oldest_idx] = now;
  }

  void clear(const mesh::Packet* packet) override {
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0) {
        memset(sp, 0, MAX_HASH_SIZE);
        _last_seen[i] = 0;
        break;
      }
    }
  }

  uint32_t getNumDirectDups() const { return _direct_dups; }
  uint32_t getNumFloodDups() const { return _flood_dups; }

  void resetStats() { _direct_dups = _flood_dups = 0; }
};
