#pragma once
#include <Mesh.h>
#include <Identity.h>
#include "SimRuntime.h"
#include "SimRadio.h"
#include "RoutingStrategies.h"
#include "DensityEstimator.h"
#include <string>
#include <vector>
#include <functional>

namespace sim {

// -------------------------------------------------------------------------
// Per-packet delivery event — recorded whenever a node receives a message.
// -------------------------------------------------------------------------
struct DeliveryEvent {
  int     src_node;
  int     dst_node;        // -1 = broadcast/flood
  int     recv_node;
  uint8_t payload_type;
  uint8_t route_type;
  uint8_t hop_count;
  float   snr;
  float   rssi;
  uint64_t tx_time_ms;
  uint64_t rx_time_ms;
  uint32_t latency_ms;
  uint32_t airtime_ms;
  bool    is_duplicate;
};

// -------------------------------------------------------------------------
// SimNode — a full MeshCore node running in simulation.
// Subclasses Mesh to intercept packets and record metrics.
// -------------------------------------------------------------------------
class SimNode : public mesh::Mesh {
public:
  int node_idx;
  std::string name;

  // Routing strategy — set before first flood, after addNode()
  RoutingStrategy routing_strategy = RoutingStrategy::DEFAULT;

  // Probabilistic forwarding — probability [0,1] that this node relays a flood.
  // 1.0 = always forward (default). Values < 1.0 randomly drop relays to reduce
  // broadcast storms at high node density. ADAPTIVE strategy overrides this
  // dynamically each packet based on current density tier.
  float p_forward = 1.0f;

  // Passive neighbor density estimator — fed by logRx(), consumed by getRetransmitDelay().
  DensityEstimator density;

  // Metrics
  std::vector<DeliveryEvent> deliveries;
  uint32_t total_tx_packets = 0;
  uint32_t total_rx_packets = 0;
  uint32_t total_duplicates = 0;
  uint64_t total_airtime_ms = 0;
  uint32_t total_suppressed = 0;   // relay cancellations from suppression logic

  // last advert tracking — used by SimBus to match receipts to injections
  uint32_t last_advert_ts = 0;
  uint32_t last_advert_src_key = 0;  // first 4 bytes of sender pub key (unique per node)

  // Callback: called when any message payload arrives at this node
  using OnRecvCallback = std::function<void(SimNode*, const DeliveryEvent&)>;
  OnRecvCallback on_recv;

  SimNode(int idx, const std::string& name,
          SimRadio& radio, SimMillisClock& ms, SimRNG& rng,
          SimRTCClock& rtc, SimPacketManager& mgr, SimTables& tables)
    : mesh::Mesh(radio, ms, rng, rtc, mgr, tables)
    , node_idx(idx), name(name)
    , _radio_ref(radio), _ms_ref(ms), _rng_ref(rng), _mgr_ref(mgr), _tables_ref(tables)
  {}

  void init() {
    // LocalIdentity(RNG*) generates a fresh Ed25519 keypair using the node's seeded RNG
    self_id = mesh::LocalIdentity(&_rng_ref);
    begin();
  }

  bool allowPacketForward(const mesh::Packet* pkt) override {
    return true;   // repeater mode — all nodes forward
  }

  // Duty cycle enforcement — override to set budget factor.
  // duty_cycle = 1 / (1 + factor):
  //   factor=1   → 50% (default, no enforcement)
  //   factor=99  → 1%  (EU legal limit)
  //   factor=9   → 10% (relaxed US usage)
  // Default: no enforcement (matches stock MeshCore behaviour).
  float duty_cycle_factor = 1.0f;  // set externally to enforce a limit
  float getAirtimeBudgetFactor() const override { return duty_cycle_factor; }

  // -----------------------------------------------------------------------
  // Power-save mode — reduces TX power when density is DENSE or MEDIUM.
  //
  // power_save_enabled:  activates adaptive TX power reduction
  // power_save_dense_dbm: TX power (dBm) when DENSE (default 20 = full power)
  // power_save_medium_dbm: TX power (dBm) when MEDIUM
  //
  // Typical LoRa TX current:
  //   +20 dBm → ~120 mA  (SX1262 PA boost)
  //   +17 dBm →  ~90 mA
  //   +14 dBm →  ~45 mA
  //   +10 dBm →  ~25 mA
  //
  // Battery model: energy_mah = sum over all TX of (tx_current_mA × airtime_s / 3600)
  //                           + rx_current_mA × total_rx_time_s / 3600
  // -----------------------------------------------------------------------
  bool  power_save_enabled    = false;
  float power_save_dense_dbm  = 10.0f;   // -10 dBm from 20 dBm max
  float power_save_medium_dbm = 14.0f;   // -6 dBm from 20 dBm max
  float full_power_dbm        = 20.0f;   // reference full power

  // Energy accounting (milliamp-hours consumed)
  float total_tx_energy_mah   = 0.0f;
  float total_rx_energy_mah   = 0.0f;
  uint64_t total_rx_time_ms   = 0;       // time spent in RX mode (not TX)

  // Current draw model (mA) — SX1262-based typical values
  static float txCurrentMa(float dbm) {
    if (dbm >= 20.0f) return 120.0f;
    if (dbm >= 17.0f) return  90.0f;
    if (dbm >= 14.0f) return  45.0f;
    if (dbm >= 10.0f) return  25.0f;
    return 15.0f;   // low power / beacon mode
  }
  static constexpr float RX_CURRENT_MA   =  5.5f;  // SX1262 in RX continuous
  static constexpr float IDLE_CURRENT_MA =  1.6f;  // SX1262 sleep/standby

protected:
  // -----------------------------------------------------------------------
  // Routing strategy dispatch — overrides base mesh::Mesh backoff.
  // -----------------------------------------------------------------------
  uint32_t getRetransmitDelay(const mesh::Packet* pkt) override {
    uint64_t now_ms = (uint64_t)_ms_ref.getMillis();

    if (routing_strategy == RoutingStrategy::ADAPTIVE) {
      DensityTier tier = density.tier(now_ms);
      int relay_pct = adaptiveRelayPct(tier);

      if (relay_pct < 100) {
        // Deterministic hash-based relay selection.
        // packet_seed: first 4 bytes of payload (stable across all recipients).
        // node_seed: stable per-node identity (pub key prefix in firmware).
        uint32_t pkt_seed = 0, node_seed = (uint32_t)node_idx * 0x9e3779b9u;
        if (pkt->getRawLength() >= 4) memcpy(&pkt_seed, pkt->payload, 4);

        if (!hashBasedRelay(pkt_seed, node_seed, relay_pct)) {
          return 999999u;  // not selected — silent
        }
      }

      // Power-save: selected relay nodes reduce TX power based on density tier.
      // This lowers received SNR at distant nodes (shrinks interference radius)
      // while nearby nodes still receive well above LoRa sensitivity.
      if (power_save_enabled) {
        switch (tier) {
          case DensityTier::DENSE:
            _radio_ref.tx_power_dbm = power_save_dense_dbm;
            break;
          case DensityTier::MEDIUM:
            _radio_ref.tx_power_dbm = power_save_medium_dbm;
            break;
          case DensityTier::SPARSE:
            _radio_ref.tx_power_dbm = full_power_dbm;
            break;
        }
      }
    }

    // Non-ADAPTIVE strategies always transmit at full power
    if (routing_strategy != RoutingStrategy::ADAPTIVE) {
      _radio_ref.tx_power_dbm = full_power_dbm;
    }

    // Manual p_forward gate (for non-ADAPTIVE strategies or explicit p_forward override)
    if (routing_strategy != RoutingStrategy::ADAPTIVE && p_forward < 1.0f) {
      uint32_t roll = _rng_ref.nextInt(0, 10000);
      if ((float)roll / 10000.0f > p_forward) {
        return 999999u;
      }
    }

    uint32_t base = (_radio_ref.getEstAirtimeFor(pkt->getRawLength()) * 52 / 50) / 2;
    if (base == 0) base = 10;

    float snr    = _radio_ref.getLastSNR();
    uint8_t hops = pkt->getPathHashCount();
    uint32_t rv  = _rng_ref.nextInt(0, 10000);

    switch (routing_strategy) {
      case RoutingStrategy::SNR_WEIGHTED:
        return snrWeightedDelay(base, snr, rv);

      case RoutingStrategy::PATH_SNR_HYBRID:
        return pathSnrHybridDelay(base, snr, hops, rv);

      case RoutingStrategy::ADAPTIVE:
        return adaptiveDelay(base, density.tier(now_ms), snr, hops, rv);

      case RoutingStrategy::DEFAULT:
      default:
        return _rng_ref.nextInt(0, 5) * base;
    }
  }


  void logRx(mesh::Packet* pkt, int len, float score) override {
    total_rx_packets++;
    uint32_t at = _radio_ref.getEstAirtimeFor(len);
    total_airtime_ms += at;
    total_rx_time_ms += at;
    total_rx_energy_mah += RX_CURRENT_MA * (float)at / 3600000.0f;

    if (!pkt || len < 4) return;

    uint64_t now_ms = (uint64_t)_ms_ref.getMillis();

    // Feed density estimator — use the actual transmitting node's ID so each
    // relay of the same flood counts as a separate sender observation.
    // In firmware, this would use a hash of the received frame's sync word or
    // a time-of-arrival fingerprint; in sim we have the exact sender index.
    {
      int sender = _radio_ref.getLastSender();
      uint32_t src_id = sender >= 0 ? (uint32_t)sender : 0u;
      density.observe(src_id, pkt->getPathHashCount(), now_ms);
    }

    // Relay suppression — cancel a queued outbound if we hear another node
    // successfully relay the same flood (hash-matched). Works for all strategies.
    // In ADAPTIVE, the hash gate already prevents most nodes from queuing at all;
    // this catches the edge case where two selected relays race.
    if (pkt->isRouteFlood()) {
      uint8_t pkt_hash[MAX_HASH_SIZE];
      pkt->calculatePacketHash(pkt_hash);

      int total = _mgr_ref.getOutboundTotal();
      for (int i = total - 1; i >= 0; i--) {
        mesh::Packet* queued = _mgr_ref.getOutboundByIdx(i);
        if (!queued) continue;
        uint8_t q_hash[MAX_HASH_SIZE];
        queued->calculatePacketHash(q_hash);
        if (memcmp(pkt_hash, q_hash, MAX_HASH_SIZE) == 0) {
          _mgr_ref.removeOutboundByIdx(i);
          total_suppressed++;
          break;
        }
      }
    }
  }

  void logTx(mesh::Packet* pkt, int len) override {
    total_tx_packets++;
    uint32_t at = _radio_ref.getEstAirtimeFor(len);
    total_airtime_ms += at;
    // Energy: TX current × airtime. Current depends on TX power at moment of transmission.
    float current_ma = txCurrentMa(_radio_ref.tx_power_dbm);
    total_tx_energy_mah += current_ma * (float)at / 3600000.0f;
  }

  void onAdvertRecv(mesh::Packet* pkt, const mesh::Identity& id,
                    uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) override
  {
    last_advert_ts = timestamp;
    memcpy(&last_advert_src_key, id.pub_key, 4);
    recordDelivery(pkt, PAYLOAD_TYPE_ADVERT, -1);
  }

  void onPeerDataRecv(mesh::Packet* pkt, uint8_t type, int sender_idx,
                      const uint8_t* secret, uint8_t* data, size_t len) override
  {
    recordDelivery(pkt, type, sender_idx);
  }

  void onGroupDataRecv(mesh::Packet* pkt, uint8_t type, const mesh::GroupChannel& ch,
                       uint8_t* data, size_t len) override
  {
    recordDelivery(pkt, type, -1);
  }

  void onRawDataRecv(mesh::Packet* pkt) override {
    recordDelivery(pkt, PAYLOAD_TYPE_RAW_CUSTOM, -1);
  }

private:
public:
  SimRadio&        _radio_ref;
  SimMillisClock&  _ms_ref;
  SimRNG&          _rng_ref;
  SimPacketManager& _mgr_ref;
  SimTables&       _tables_ref;

  void recordDelivery(mesh::Packet* pkt, uint8_t payload_type, int sender_idx) {
    uint64_t now = (uint64_t)_ms_ref.getMillis();

    DeliveryEvent ev;
    ev.src_node    = sender_idx;
    ev.dst_node    = -1;
    ev.recv_node   = node_idx;
    ev.payload_type = payload_type;
    ev.route_type  = pkt->getRouteType();
    ev.hop_count   = pkt->getPathHashCount();
    ev.snr         = pkt->getSNR();
    ev.rssi        = _radio_ref.getLastRSSI();
    ev.tx_time_ms  = 0;   // filled in by SimBus when known
    ev.rx_time_ms  = now;
    ev.latency_ms  = 0;
    ev.airtime_ms  = _radio_ref.getEstAirtimeFor(pkt->getRawLength());
    ev.is_duplicate = false;

    deliveries.push_back(ev);

    if (on_recv) on_recv(this, ev);
  }
};

}
