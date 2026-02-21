#pragma once

#include <Mesh.h>

#ifdef ESP32
  #include <FS.h>
#endif

#define MAX_PACKET_HASHES  128
#define MAX_PACKET_ACKS     64

class SimpleMeshTables : public mesh::MeshTables {
  uint8_t _hashes[MAX_PACKET_HASHES*MAX_HASH_SIZE];
  int _next_idx;
  uint32_t _acks[MAX_PACKET_ACKS];
  int _next_ack_idx;
  uint32_t _hash_prefix_bits[8];
  uint32_t _ack_lsb_bits[8];
  uint32_t _direct_dups, _flood_dups;

  static bool hasFastBit(const uint32_t bits[], uint8_t key) {
    return (bits[key >> 5] & (1UL << (key & 31))) != 0;
  }
  static void setFastBit(uint32_t bits[], uint8_t key) {
    bits[key >> 5] |= (1UL << (key & 31));
  }
  void rebuildFastBits() {
    memset(_hash_prefix_bits, 0, sizeof(_hash_prefix_bits));
    memset(_ack_lsb_bits, 0, sizeof(_ack_lsb_bits));
    for (int i = 0; i < MAX_PACKET_HASHES; i++) {
      const uint8_t* hp = &_hashes[i * MAX_HASH_SIZE];
      if (hp[0] | hp[1] | hp[2] | hp[3] | hp[4] | hp[5] | hp[6] | hp[7]) {
        setFastBit(_hash_prefix_bits, hp[0]);
      }
    }
    for (int i = 0; i < MAX_PACKET_ACKS; i++) {
      if (_acks[i] != 0) {
        setFastBit(_ack_lsb_bits, (uint8_t)_acks[i]);
      }
    }
  }

public:
  SimpleMeshTables() { 
    memset(_hashes, 0, sizeof(_hashes));
    _next_idx = 0;
    memset(_acks, 0, sizeof(_acks));
    _next_ack_idx = 0;
    memset(_hash_prefix_bits, 0, sizeof(_hash_prefix_bits));
    memset(_ack_lsb_bits, 0, sizeof(_ack_lsb_bits));
    _direct_dups = _flood_dups = 0;
  }

#ifdef ESP32
  void restoreFrom(File f) {
    f.read(_hashes, sizeof(_hashes));
    f.read((uint8_t *) &_next_idx, sizeof(_next_idx));
    f.read((uint8_t *) &_acks[0], sizeof(_acks));
    f.read((uint8_t *) &_next_ack_idx, sizeof(_next_ack_idx));
    rebuildFastBits();
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
      uint32_t ack;
      memcpy(&ack, packet->payload, 4);
      if (!hasFastBit(_ack_lsb_bits, (uint8_t)ack)) {
        _acks[_next_ack_idx] = ack;
        _next_ack_idx = (_next_ack_idx + 1) % MAX_PACKET_ACKS;  // cyclic table
        setFastBit(_ack_lsb_bits, (uint8_t)ack);
        return false;
      }
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
      setFastBit(_ack_lsb_bits, (uint8_t)ack);
      return false;
    }

    uint8_t hash[MAX_HASH_SIZE];
    packet->calculatePacketHash(hash);
    if (!hasFastBit(_hash_prefix_bits, hash[0])) {
      memcpy(&_hashes[_next_idx * MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
      _next_idx = (_next_idx + 1) % MAX_PACKET_HASHES;  // cyclic table
      setFastBit(_hash_prefix_bits, hash[0]);
      return false;
    }

    const uint8_t* sp = _hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += MAX_HASH_SIZE) {
      if (memcmp(hash, sp, MAX_HASH_SIZE) == 0) { 
        if (packet->isRouteDirect()) {
          _direct_dups++;   // keep some stats
        } else {
          _flood_dups++;
        }
        return true;
      }
    }

    memcpy(&_hashes[_next_idx*MAX_HASH_SIZE], hash, MAX_HASH_SIZE);
    _next_idx = (_next_idx + 1) % MAX_PACKET_HASHES;  // cyclic table
    setFastBit(_hash_prefix_bits, hash[0]);
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
          break;
        }
      }
    }
  }

  uint32_t getNumDirectDups() const { return _direct_dups; }
  uint32_t getNumFloodDups() const { return _flood_dups; }

  void resetStats() { _direct_dups = _flood_dups = 0; }
};
