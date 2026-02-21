#pragma once

#include <Arduino.h>
#include <Mesh.h>

#ifdef ESP32
  #include <FS.h>
#endif

#define MAX_PACKET_HASHES  128
#define MAX_PACKET_ACKS     64
#define ACK_DEDUP_WINDOW_MILLIS   60000UL
#define DATA_DEDUP_WINDOW_MILLIS 120000UL

class SimpleMeshTables : public mesh::MeshTables {
  uint8_t _hashes[MAX_PACKET_HASHES*MAX_HASH_SIZE];
  uint32_t _hash_seen_at[MAX_PACKET_HASHES];
  int _next_idx;
  uint32_t _acks[MAX_PACKET_ACKS];
  uint32_t _ack_seen_at[MAX_PACKET_ACKS];
  int _next_ack_idx;
  uint32_t _direct_dups, _flood_dups;
  uint32_t _ack_hits, _data_hits, _overwrite_when_full;

public:
  SimpleMeshTables() { 
    memset(_hashes, 0, sizeof(_hashes));
    memset(_hash_seen_at, 0, sizeof(_hash_seen_at));
    _next_idx = 0;
    memset(_acks, 0, sizeof(_acks));
    memset(_ack_seen_at, 0, sizeof(_ack_seen_at));
    _next_ack_idx = 0;
    _direct_dups = _flood_dups = 0;
    _ack_hits = _data_hits = _overwrite_when_full = 0;
  }

#ifdef ESP32
  void restoreFrom(File f) {
    f.read(_hashes, sizeof(_hashes));
    f.read((uint8_t *) &_next_idx, sizeof(_next_idx));
    f.read((uint8_t *) &_acks[0], sizeof(_acks));
    f.read((uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
    memset(_hash_seen_at, 0, sizeof(_hash_seen_at));
    memset(_ack_seen_at, 0, sizeof(_ack_seen_at));
  }
  void saveTo(File f) {
    f.write(_hashes, sizeof(_hashes));
    f.write((const uint8_t *) &_next_idx, sizeof(_next_idx));
    f.write((const uint8_t *) &_acks[0], sizeof(_acks));
    f.write((const uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
  }
#endif

  bool hasSeen(const mesh::Packet* packet) override {
    uint32_t now = millis();
    if (packet->getPayloadType() == PAYLOAD_TYPE_ACK) {
      uint32_t ack;
      memcpy(&ack, packet->payload, 4);
      int empty_idx = -1;
      int expired_idx = -1;
      for (int i = 0; i < MAX_PACKET_ACKS; i++) {
        if (_ack_seen_at[i] == 0) {
          if (empty_idx < 0) empty_idx = i;
          continue;
        }
        uint32_t age = now - _ack_seen_at[i];
        if (age > ACK_DEDUP_WINDOW_MILLIS) {
          if (expired_idx < 0) expired_idx = i;
          continue;
        }
        if (ack == _acks[i]) { 
          _ack_hits++;
          if (packet->isRouteDirect()) {
            _direct_dups++;   // keep some stats
          } else {
            _flood_dups++;
          }
          return true;
        }
      }

      int use_idx = (expired_idx >= 0) ? expired_idx : empty_idx;
      if (use_idx < 0) {
        use_idx = _next_ack_idx;
        _next_ack_idx = (_next_ack_idx + 1) % MAX_PACKET_ACKS;  // cyclic table
        _overwrite_when_full++;
      }
      _acks[use_idx] = ack;
      _ack_seen_at[use_idx] = now;
      return false;
    }

    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);

    int empty_idx = -1;
    int expired_idx = -1;
    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      if (_hash_seen_at[i] == 0) {
        if (empty_idx < 0) empty_idx = i;
        continue;
      }
      uint32_t age = now - _hash_seen_at[i];
      if (age > DATA_DEDUP_WINDOW_MILLIS) {
        if (expired_idx < 0) expired_idx = i;
        continue;
      }
      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0) { 
        _data_hits++;
        if (packet->isRouteDirect()) {
          _direct_dups++;   // keep some stats
        } else {
          _flood_dups++;
        }
        return true;
      }
    }

    int use_idx = (expired_idx >= 0) ? expired_idx : empty_idx;
    if (use_idx < 0) {
      use_idx = _next_idx;
      _next_idx = (_next_idx + 1) % MAX_PACKET_HASHES;  // cyclic table
      _overwrite_when_full++;
    }
    memcpy(&_hashes[use_idx*MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
    _hash_seen_at[use_idx] = now;
    return false;
  }

  void clear(const mesh::Packet* packet) override {
    if (packet->getPayloadType() == PAYLOAD_TYPE_ACK) {
      uint32_t ack;
      memcpy(&ack, packet->payload, 4);
      for (int i = 0; i < MAX_PACKET_ACKS; i++) {
        if (ack == _acks[i]) { 
          _acks[i] = 0;
          _ack_seen_at[i] = 0;
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
          _hash_seen_at[i] = 0;
          break;
        }
      }
    }
  }

  uint32_t getNumDirectDups() const { return _direct_dups; }
  uint32_t getNumFloodDups() const { return _flood_dups; }
  uint32_t getNumAckHits() const { return _ack_hits; }
  uint32_t getNumDataHits() const { return _data_hits; }
  uint32_t getNumOverwriteWhenFull() const { return _overwrite_when_full; }

  void resetStats() { _direct_dups = _flood_dups = _ack_hits = _data_hits = _overwrite_when_full = 0; }
};
