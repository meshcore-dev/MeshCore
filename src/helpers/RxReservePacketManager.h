#pragma once

#include <MeshCore.h>
#include <helpers/StaticPoolPacketManager.h>

// Fork-owned (not upstream-tracked). Observer builds capture every received packet
// to MQTT, but RX processing needs a free pool packet first — Dispatcher::checkRecv()
// discards the received bytes before logRx() when allocNew() fails. Under duty-cycle
// throttling the outbound queue can park the entire pool waiting on TX budget, which
// starves RX allocation and silently caps MQTT capture at the TX rate (each completed
// TX frees exactly one packet for exactly one more RX). Parked retransmissions also
// absorb every budget refill, starving the node's own CLI responses/ACKs and making
// a heavily-throttled node un-administrable over the mesh.
//
// Two policies fix this, both confined to this manager:
//
// 1. Priority-aware shedding. Below the RX reserve, only low-priority outbound
//    (priority > 1: multi-hop flood repeats, adverts, trace) is refused; the node's
//    own responses/ACKs (pri 0) and login/PATH replies (pri 1) still queue. Below the
//    smaller emergency floor everything is shed to keep capture alive.
// 2. Stale-packet expiry. A queued packet still untransmitted STALE_OUTBOUND_MS past
//    its scheduled time is dropped at the next dequeue — a repeat delayed that long is
//    noise (the flood has long since propagated), and a CLI response that old has
//    already timed out at the client. Under normal load the queue drains in
//    milliseconds and this never triggers; under throttle it frees the pool and lets
//    fresh traffic (including admin responses) compete for the trickle of TX budget.
class RxReservePacketManager : public StaticPoolPacketManager {
  int _rx_reserve, _emergency_floor;
  int _cap;
  // scheduled_for per queued packet, keyed by packet pointer. The pool is a fixed set
  // of _cap Packet objects, so _cap slots cover every possible key with no eviction.
  struct AgeEntry { mesh::Packet* pkt; uint32_t scheduled_for; };
  AgeEntry* _ages;

  static const uint32_t STALE_OUTBOUND_MS = 30000;
  static const uint8_t MAX_PROTECTED_PRI = 1;  // pri 0-1 = own responses/ACKs/replies

  void recordAge(mesh::Packet* packet, uint32_t scheduled_for) {
    int empty = -1;
    for (int i = 0; i < _cap; i++) {
      if (_ages[i].pkt == packet) { _ages[i].scheduled_for = scheduled_for; return; }
      if (empty < 0 && _ages[i].pkt == NULL) empty = i;
    }
    if (empty >= 0) { _ages[empty].pkt = packet; _ages[empty].scheduled_for = scheduled_for; }
  }

  bool lookupAge(const mesh::Packet* packet, uint32_t* scheduled_for) const {
    for (int i = 0; i < _cap; i++) {
      if (_ages[i].pkt == packet) { *scheduled_for = _ages[i].scheduled_for; return true; }
    }
    return false;
  }

public:
  RxReservePacketManager(int pool_size, int rx_reserve)
    : StaticPoolPacketManager(pool_size), _rx_reserve(rx_reserve),
      _emergency_floor(rx_reserve / 2), _cap(pool_size) {
    _ages = new AgeEntry[pool_size];
    for (int i = 0; i < pool_size; i++) { _ages[i].pkt = NULL; _ages[i].scheduled_for = 0; }
  }

  void queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) override {
    int free_count = getFreeCount();
    if (free_count < _emergency_floor
        || (free_count < _rx_reserve && priority > MAX_PROTECTED_PRI)) {
      MESH_DEBUG_PRINTLN("RxReservePacketManager: pool below RX reserve, shedding outbound (pri %d)", (int)priority);
      free(packet);
      return;
    }
    recordAge(packet, scheduled_for);
    StaticPoolPacketManager::queueOutbound(packet, priority, scheduled_for);
  }

  mesh::Packet* getNextOutbound(uint32_t now) override {
    // Expire queued packets that have waited too long past their scheduled time.
    for (int i = getOutboundTotal() - 1; i >= 0; i--) {
      mesh::Packet* pkt = getOutboundByIdx(i);
      uint32_t scheduled_for;
      if (pkt && lookupAge(pkt, &scheduled_for)
          && (int32_t)(now - scheduled_for) > (int32_t)STALE_OUTBOUND_MS) {
        MESH_DEBUG_PRINTLN("RxReservePacketManager: dropping stale queued outbound");
        removeOutboundByIdx(i);
        free(pkt);
      }
    }
    return StaticPoolPacketManager::getNextOutbound(now);
  }
};

// The packet manager for an app build: observer builds reserve a quarter of the pool
// for RX so MQTT capture survives duty-cycle throttling; non-observer builds keep the
// upstream pool behavior unchanged.
inline mesh::PacketManager* createObserverPacketManager(int pool_size) {
#ifdef WITH_MQTT_BRIDGE
  return new RxReservePacketManager(pool_size, pool_size / 4);
#else
  return new StaticPoolPacketManager(pool_size);
#endif
}
