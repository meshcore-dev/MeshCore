#pragma once
#include <MeshCore.h>
#include <Dispatcher.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <deque>
#include <algorithm>

namespace sim {

// -------------------------------------------------------------------------
// SimClock — shared wall clock for all nodes in a simulation run.
// -------------------------------------------------------------------------
class SimClock {
  uint64_t _ms = 0;
public:
  uint64_t now() const { return _ms; }
  void advance(uint64_t delta_ms) { _ms += delta_ms; }
  void set(uint64_t ms) { _ms = ms; }
};

// -------------------------------------------------------------------------
// SimMillisClock — per-node MillisecondClock backed by SimClock.
// -------------------------------------------------------------------------
class SimMillisClock : public mesh::MillisecondClock {
  SimClock& _clock;
public:
  SimMillisClock(SimClock& clock) : _clock(clock) {}
  unsigned long getMillis() override { return (unsigned long)_clock.now(); }
};

// -------------------------------------------------------------------------
// SimRTCClock — per-node RTCClock.
// -------------------------------------------------------------------------
class SimRTCClock : public mesh::RTCClock {
  uint32_t _epoch;
  SimClock& _clock;
  uint64_t _start_ms;
public:
  SimRTCClock(SimClock& clock, uint32_t start_epoch = 1700000000)
    : _clock(clock), _epoch(start_epoch), _start_ms(clock.now()) {}

  uint32_t getCurrentTime() override {
    return _epoch + (uint32_t)((_clock.now() - _start_ms) / 1000);
  }
  void setCurrentTime(uint32_t t) override { _epoch = t; _start_ms = _clock.now(); }
};

// -------------------------------------------------------------------------
// SimRNG — deterministic PRNG for reproducible tests.
// -------------------------------------------------------------------------
class SimRNG : public mesh::RNG {
  uint32_t _state;
public:
  SimRNG(uint32_t seed = 42) : _state(seed) {}

  // xorshift32
  uint32_t raw() {
    _state ^= _state << 13;
    _state ^= _state >> 17;
    _state ^= _state << 5;
    return _state;
  }

  // Only pure virtual in mesh::RNG — nextInt() is non-virtual, implemented in Utils.cpp
  void random(uint8_t* buf, size_t len) override {
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)raw();
  }
};

// -------------------------------------------------------------------------
// SimPacketPool — static pool of Packet objects + outbound/inbound queues.
// -------------------------------------------------------------------------
#define SIM_PACKET_POOL_SIZE 32

struct QueuedPacket {
  mesh::Packet* pkt;
  uint8_t priority;
  uint64_t scheduled_for;
};

class SimPacketManager : public mesh::PacketManager {
  mesh::Packet _pool[SIM_PACKET_POOL_SIZE];
  bool _in_use[SIM_PACKET_POOL_SIZE];
  std::vector<QueuedPacket> _outbound;
  std::vector<QueuedPacket> _inbound;
  SimClock& _clock;

public:
  SimPacketManager(SimClock& clock) : _clock(clock) {
    memset(_in_use, 0, sizeof(_in_use));
  }

  mesh::Packet* allocNew() override {
    for (int i = 0; i < SIM_PACKET_POOL_SIZE; i++) {
      if (!_in_use[i]) {
        _in_use[i] = true;
        return &_pool[i];
      }
    }
    return nullptr;
  }

  void free(mesh::Packet* p) override {
    for (int i = 0; i < SIM_PACKET_POOL_SIZE; i++) {
      if (&_pool[i] == p) { _in_use[i] = false; return; }
    }
  }

  void queueOutbound(mesh::Packet* pkt, uint8_t priority, uint32_t scheduled_for) override {
    _outbound.push_back({pkt, priority, (uint64_t)scheduled_for});
  }

  mesh::Packet* getNextOutbound(uint32_t now) override {
    int best = -1;
    for (int i = 0; i < (int)_outbound.size(); i++) {
      if (_outbound[i].scheduled_for <= now) {
        if (best < 0 || _outbound[i].priority > _outbound[best].priority)
          best = i;
      }
    }
    if (best < 0) return nullptr;
    auto q = _outbound[best];
    _outbound.erase(_outbound.begin() + best);
    return q.pkt;
  }

  int getOutboundCount(uint32_t now) const override {
    int n = 0;
    for (auto& q : _outbound) if (q.scheduled_for <= now) n++;
    return n;
  }

  int getOutboundTotal() const override { return (int)_outbound.size(); }
  int getFreeCount() const override {
    int n = 0;
    for (int i = 0; i < SIM_PACKET_POOL_SIZE; i++) if (!_in_use[i]) n++;
    return n;
  }

  mesh::Packet* getOutboundByIdx(int i) override {
    if (i < 0 || i >= (int)_outbound.size()) return nullptr;
    return _outbound[i].pkt;
  }

  mesh::Packet* removeOutboundByIdx(int i) override {
    if (i < 0 || i >= (int)_outbound.size()) return nullptr;
    auto p = _outbound[i].pkt;
    _outbound.erase(_outbound.begin() + i);
    return p;
  }

  void queueInbound(mesh::Packet* pkt, uint32_t scheduled_for) override {
    _inbound.push_back({pkt, 0, (uint64_t)scheduled_for});
  }

  mesh::Packet* getNextInbound(uint32_t now) override {
    for (int i = 0; i < (int)_inbound.size(); i++) {
      if (_inbound[i].scheduled_for <= now) {
        auto p = _inbound[i].pkt;
        _inbound.erase(_inbound.begin() + i);
        return p;
      }
    }
    return nullptr;
  }
};

// -------------------------------------------------------------------------
// SimTables — seen-packet deduplication table.
// -------------------------------------------------------------------------
class SimTables : public mesh::MeshTables {
  static const int TABLE_SIZE = 64;
  uint8_t _hashes[TABLE_SIZE][MAX_HASH_SIZE];
  int _count = 0;

public:
  bool hasSeen(const mesh::Packet* pkt) override {
    uint8_t h[MAX_HASH_SIZE];
    pkt->calculatePacketHash(h);
    for (int i = 0; i < _count; i++) {
      if (memcmp(_hashes[i], h, MAX_HASH_SIZE) == 0) return true;
    }
    if (_count < TABLE_SIZE) {
      memcpy(_hashes[_count++], h, MAX_HASH_SIZE);
    } else {
      // ring-buffer evict oldest
      memmove(_hashes[0], _hashes[1], (TABLE_SIZE-1) * MAX_HASH_SIZE);
      memcpy(_hashes[TABLE_SIZE-1], h, MAX_HASH_SIZE);
    }
    return false;
  }

  void clear(const mesh::Packet* pkt) override {
    uint8_t h[MAX_HASH_SIZE];
    pkt->calculatePacketHash(h);
    for (int i = 0; i < _count; i++) {
      if (memcmp(_hashes[i], h, MAX_HASH_SIZE) == 0) {
        memmove(_hashes[i], _hashes[i+1], (_count - i - 1) * MAX_HASH_SIZE);
        _count--;
        return;
      }
    }
  }

  void reset() { _count = 0; }
};

// -------------------------------------------------------------------------
// SimMainBoard — stub board implementation.
// -------------------------------------------------------------------------
class SimMainBoard : public mesh::MainBoard {
  int _node_idx;
public:
  SimMainBoard(int idx) : _node_idx(idx) {}
  uint16_t getBattMilliVolts() override { return 3700; }
  const char* getManufacturerName() const override { return "Sim"; }
  void reboot() override {}
  uint8_t getStartupReason() const override { return 0; }
};

}
