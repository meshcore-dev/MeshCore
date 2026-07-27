#pragma once

#include <Mesh.h>

#ifdef ESP32
  #include <FS.h>
#endif

#define MAX_PACKET_HASHES  (128+32)

class SimpleMeshTables : public mesh::MeshTables {
  enum SeenType : uint8_t {
    SEEN_DISPATCH = 0x01,
    SEEN_FORWARD = 0x02,
    SEEN_BOTH = SEEN_DISPATCH | SEEN_FORWARD,
  };

  uint8_t _hashes[MAX_PACKET_HASHES*MAX_HASH_SIZE];
  uint8_t _seen_types[MAX_PACKET_HASHES];
  int _next_idx;
  uint32_t _direct_dups, _flood_dups;

public:
  SimpleMeshTables() { 
    memset(_hashes, 0, sizeof(_hashes));
    memset(_seen_types, 0, sizeof(_seen_types));
    _next_idx = 0;
    _direct_dups = _flood_dups = 0;
  }

#ifdef ESP32
  void restoreFrom(File f) {
    f.read(_hashes, sizeof(_hashes));
    memset(_seen_types, SEEN_BOTH, sizeof(_seen_types));
    f.read((uint8_t *) &_next_idx, sizeof(_next_idx));
  }
  void saveTo(File f) {
    f.write(_hashes, sizeof(_hashes));
    f.write((const uint8_t *) &_next_idx, sizeof(_next_idx));
  }
#endif

  int findHash(const uint8_t hash[]) const {
    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0) return i;
    }
    return -1;
  }

  bool hasType(const mesh::Packet* packet, uint8_t seen_type, bool count_dup) {
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    int idx = findHash(hash);
    if (idx < 0 || (_seen_types[idx] & seen_type) == 0) return false;

    if (count_dup) {
      if (packet->isRouteDirect()) {
        _direct_dups++;
      } else {
        _flood_dups++;
      }
    }
    return true;
  }

  void markType(const mesh::Packet* packet, uint8_t seen_type) {
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    int idx = findHash(hash);
    if (idx < 0) {
      idx = _next_idx;
      memcpy(&_hashes[idx * MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
      _seen_types[idx] = 0;
      _next_idx = (_next_idx + 1) % MAX_PACKET_HASHES;
    }
    _seen_types[idx] |= seen_type;
  }

  bool wasSeen(const mesh::Packet* packet) override {
    return hasType(packet, SEEN_DISPATCH, true);
  }

  void markSeen(const mesh::Packet* packet) override {
    markType(packet, SEEN_DISPATCH);
  }

  bool wasForwarded(const mesh::Packet* packet) override {
    return hasType(packet, SEEN_FORWARD, false);
  }

  void markForwarded(const mesh::Packet* packet) override {
    markType(packet, SEEN_FORWARD);
  }

  void clear(const mesh::Packet* packet) override {
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    int idx = findHash(hash);
    if (idx >= 0) {
      memset(&_hashes[idx * MAX_HASH_SIZE], 0, MAX_HASH_SIZE);
      _seen_types[idx] = 0;
    }
  }

  uint32_t getNumDirectDups() const { return _direct_dups; }
  uint32_t getNumFloodDups() const { return _flood_dups; }

  void resetStats() { _direct_dups = _flood_dups = 0; }
};
