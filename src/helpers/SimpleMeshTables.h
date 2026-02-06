#pragma once

#include <Mesh.h>

#ifdef ESP32
  #include <FS.h>
#endif

#define MAX_PACKET_HASHES  128
#define MAX_PACKET_ACKS     64

class SimpleMeshTables : public mesh::MeshTables {
  uint8_t _hashes[MAX_PACKET_HASHES*MAX_HASH_SIZE];
  uint32_t _last_seen[MAX_PACKET_HASHES];  // timestamp for LRU eviction
  uint32_t _acks[MAX_PACKET_ACKS];
  int _next_ack_idx;
  uint32_t _direct_dups, _flood_dups;

public:
  SimpleMeshTables() {
    memset(_hashes, 0, sizeof(_hashes));
    memset(_last_seen, 0, sizeof(_last_seen));
    memset(_acks, 0, sizeof(_acks));
    _next_ack_idx = 0;
    _direct_dups = _flood_dups = 0;
  }

#ifdef ESP32
  void restoreFrom(File f) {
    f.read(_hashes, sizeof(_hashes));
    int dummy_idx;
    f.read((uint8_t *) &dummy_idx, sizeof(dummy_idx));  // legacy, ignore
    f.read((uint8_t *) &_acks[0], sizeof(_acks));
    f.read((uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
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
    f.write((const uint8_t *) &_acks[0], sizeof(_acks));
    f.write((const uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
  }
#endif

  bool hasSeen(const mesh::Packet* packet) override {
    if (packet->getPayloadType() == PAYLOAD_TYPE_ACK) {
      uint32_t ack;
      memcpy(&ack, packet->payload, 4);
      for (int i = 0; i < MAX_PACKET_ACKS; i++) {
        if (ack == _acks[i]) {
          if (packet->isRouteDirect()) {
            _direct_dups++;   // keep some stats
          } else {
            _flood_dups++;
          }
          return true;
        }
      }

      _acks[_next_ack_idx] = ack;
      _next_ack_idx = (_next_ack_idx + 1) % MAX_PACKET_ACKS;  // cyclic table
      return false;
    }

    uint32_t now = millis();
    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    int oldest_idx = 0;
    uint32_t oldest_age = 0;

    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      uint32_t age = now - _last_seen[i];

      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0 && _last_seen[i] != 0) {
        // Match found - refresh timestamp (LRU touch) and return true
        _last_seen[i] = now;
        if (packet->isRouteDirect()) {
          _direct_dups++;   // keep some stats
        } else {
          _flood_dups++;
        }
        return true;
      }

      // Track oldest entry for LRU eviction
      if (age > oldest_age) {
        oldest_age = age;
        oldest_idx = i;
      }
    }

    // Not found - evict oldest (LRU)
    int insert_idx = oldest_idx;
    memcpy(&_hashes[insert_idx*MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
    _last_seen[insert_idx] = now;
    return false;
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
          _last_seen[i] = 0;
          break;
        }
      }
    }
  }

  uint32_t getNumDirectDups() const { return _direct_dups; }
  uint32_t getNumFloodDups() const { return _flood_dups; }

  void resetStats() { _direct_dups = _flood_dups = 0; }
};
