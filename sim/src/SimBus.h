#pragma once
#include "SimNode.h"
#include "SimRadio.h"
#include "SimMetrics.h"
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <cstdio>

namespace sim {

// -------------------------------------------------------------------------
// Pending RF event on the bus — a packet in-flight between nodes.
// -------------------------------------------------------------------------
struct InFlightPacket {
  int     src_node;
  uint8_t bytes[255];
  int     len;
  uint64_t arrive_at_ms;  // when it becomes receivable
  float   tx_power_dbm;   // transmitter power — applied as SNR offset at receivers
};

// -------------------------------------------------------------------------
// SimBus — the virtual RF medium.
//
// Owns all nodes and their supporting objects. Drives time forward tick
// by tick, delivers packets between nodes according to the channel model,
// and collects aggregate metrics.
// -------------------------------------------------------------------------
class SimBus {
public:
  SimClock clock;

  struct NodeBundle {
    std::unique_ptr<SimMillisClock>  ms;
    std::unique_ptr<SimRTCClock>     rtc;
    std::unique_ptr<SimRNG>          rng;
    std::unique_ptr<SimPacketManager> mgr;
    std::unique_ptr<SimTables>       tables;
    std::unique_ptr<SimRadio>        radio;
    std::unique_ptr<SimNode>         node;
  };

  std::vector<NodeBundle> nodes;
  RFChannelModel* channel_model = nullptr;
  SimMetrics metrics;

  // Propagation delay in ms (default: 0 = instantaneous)
  uint32_t propagation_delay_ms = 0;

  // Tick resolution — how many ms the sim advances each step
  uint32_t tick_ms = 1;

  int _flood_seq = 0;

  uint32_t totalCollisions() const {
    uint32_t n = 0;
    for (auto& b : nodes) n += b.radio->total_collisions;
    return n;
  }

  // -----------------------------------------------------------------------
  // Add a node to the bus. Returns the node index.
  // -----------------------------------------------------------------------
  int addNode(const std::string& name, uint32_t rng_seed = 0) {
    int idx = (int)nodes.size();
    auto& b = nodes.emplace_back();

    b.ms    = std::make_unique<SimMillisClock>(clock);
    b.rtc   = std::make_unique<SimRTCClock>(clock);
    b.rng   = std::make_unique<SimRNG>(rng_seed ? rng_seed : (uint32_t)(idx * 0x9e3779b9 + 1));
    b.mgr   = std::make_unique<SimPacketManager>(clock);
    b.tables= std::make_unique<SimTables>();

    // Radio tx callback: inject into bus
    auto cb = [this, idx](int src, const uint8_t* bytes, int len, uint32_t airtime_ms, float tx_power_dbm) {
      this->onTransmit(src, bytes, len, airtime_ms, tx_power_dbm);
    };
    b.radio = std::make_unique<SimRadio>(idx, cb);
    b.node  = std::make_unique<SimNode>(idx, name, *b.radio, *b.ms, *b.rng, *b.rtc, *b.mgr, *b.tables);
    b.node->init();

    // Wire delivery events into metrics using (src_key, timestamp) as flood ID
    int idx_capture = idx;
    b.node->on_recv = [this, idx_capture](SimNode* n, const DeliveryEvent& ev) {
      uint64_t compound = ((uint64_t)n->last_advert_src_key << 32) | n->last_advert_ts;
      auto it = _tsnode_to_seq.find(compound);
      int seq = it != _tsnode_to_seq.end() ? it->second : -1;
      if (seq < 0) return;   // not a tracked flood
      uint64_t inj = _inject_times.count(seq) ? _inject_times[seq] : 0;
      auto src_it = _seq_to_src.find(seq);
      int src = src_it != _seq_to_src.end() ? src_it->second : -1;
      metrics.record(src, idx_capture, seq, inj,
                     (uint64_t)n->_ms_ref.getMillis(),
                     ev.hop_count, ev.route_type, ev.snr, ev.airtime_ms);
    };

    metrics.setNumNodes((int)nodes.size());
    return idx;
  }

  // -----------------------------------------------------------------------
  // Run simulation for `duration_ms` of simulated time.
  // -----------------------------------------------------------------------
  void run(uint64_t duration_ms) {
    uint64_t end = clock.now() + duration_ms;
    while (clock.now() < end) {
      // Reset per-tick collision state so each tick starts clean
      for (auto& b : nodes) b.radio->tickReset();
      // Deliver in-flight packets, then loop all nodes, then deliver again
      // (so newly injected packets from loop() can be received this tick)
      deliverPending();
      for (auto& b : nodes) b.node->loop();
      deliverPending();
      clock.advance(tick_ms);
    }
  }

  // -----------------------------------------------------------------------
  // Inject a raw advertisement flood from a specific node.
  // -----------------------------------------------------------------------
  void sendAdvert(int node_idx) {
    auto& b = nodes[node_idx];
    auto* pkt = b.node->createAdvert(b.node->self_id);
    if (pkt) b.node->sendFlood(pkt);
  }

  // -----------------------------------------------------------------------
  // Inject a flood advertisement from src_node (used to benchmark flood propagation).
  // Adverts are the canonical flood packet type in MeshCore.
  // -----------------------------------------------------------------------
  void sendFloodText(int node_idx, const char* text) {
    auto& b = nodes[node_idx];
    uint8_t app_data[MAX_ADVERT_DATA_SIZE];
    size_t len = strlen(text);
    if (len > MAX_ADVERT_DATA_SIZE) len = MAX_ADVERT_DATA_SIZE;
    memcpy(app_data, text, len);
    auto* pkt = b.node->createAdvert(b.node->self_id, app_data, len);
    if (pkt) {
      // Read advert timestamp (offset PUB_KEY_SIZE in payload) and sender pub key prefix.
      // Use compound key (src_key<<32|ts) so simultaneous senders with same-second RTC
      // timestamps don't collide in the tracking map.
      uint32_t ts;
      memcpy(&ts, &pkt->payload[PUB_KEY_SIZE], 4);
      uint32_t src_key = 0;
      memcpy(&src_key, b.node->self_id.pub_key, 4);
      uint64_t compound = ((uint64_t)src_key << 32) | ts;
      int seq = _flood_seq++;
      _ts_to_seq[ts] = seq;           // kept for single-source compat
      _tsnode_to_seq[compound] = seq;
      _seq_to_src[seq] = node_idx;
      _inject_times[seq] = clock.now();
      metrics.setNumNodes((int)nodes.size());
      b.node->sendFlood(pkt);
    }
  }

  // -----------------------------------------------------------------------
  // Reset all node stats for a fresh measurement window.
  // -----------------------------------------------------------------------
  void resetStats() {
    for (auto& b : nodes) {
      b.node->total_tx_packets   = 0;
      b.node->total_rx_packets   = 0;
      b.node->total_duplicates   = 0;
      b.node->total_airtime_ms   = 0;
      b.node->total_suppressed   = 0;
      b.node->total_tx_energy_mah = 0.0f;
      b.node->total_rx_energy_mah = 0.0f;
      b.node->total_rx_time_ms   = 0;
      b.node->deliveries.clear();
      b.radio->total_collisions = 0;
      b.tables->reset();
    }
    metrics.reset();
    _flood_seq = 0;
    _ts_to_seq.clear();
    _tsnode_to_seq.clear();
    _seq_to_src.clear();
    _inject_times.clear();
  }

  // -----------------------------------------------------------------------
  // Print a summary report to stdout.
  // -----------------------------------------------------------------------
  void printReport(const char* scenario_name = "sim") const {
    printf("\n=== SimBus Report: %s ===\n", scenario_name);
    printf("%-12s %8s %8s %8s %8s %8s\n",
           "Node", "TX", "RX", "Dups", "Delivs", "AirtimeMs");
    printf("%-12s %8s %8s %8s %8s %8s\n",
           "----", "--", "--", "----", "------", "---------");

    uint32_t total_tx = 0, total_rx = 0, total_deliveries = 0;
    uint64_t total_air = 0;

    for (auto& b : nodes) {
      auto& n = *b.node;
      printf("%-12s %8u %8u %8u %8zu %8llu\n",
             n.name.c_str(), n.total_tx_packets, n.total_rx_packets,
             n.total_duplicates, n.deliveries.size(), (unsigned long long)n.total_airtime_ms);
      total_tx += n.total_tx_packets;
      total_rx += n.total_rx_packets;
      total_deliveries += (uint32_t)n.deliveries.size();
      total_air += n.total_airtime_ms;
    }

    printf("%-12s %8u %8u %8s %8u %8llu\n",
           "TOTAL", total_tx, total_rx, "-", total_deliveries,
           (unsigned long long)total_air);

    // Delivery rate: out of (num_nodes - 1) possible receivers per flood
    int n = (int)nodes.size();
    if (n > 1 && total_tx > 0) {
      // count unique flood origins (TX from each node once = 1 event)
      float rate = (float)total_deliveries / (float)(total_tx * (n - 1)) * 100.0f;
      printf("\nEstimated delivery rate: %.1f%%\n", rate);
    }

    printf("Total simulated airtime: %llu ms\n", (unsigned long long)total_air);
    printf("=====================================\n");
  }

  // -----------------------------------------------------------------------
  // Dump all delivery events as CSV to a file or stdout.
  // -----------------------------------------------------------------------
  void dumpCSV(FILE* f = stdout) const {
    fprintf(f, "recv_node,payload_type,route_type,hop_count,snr,rx_time_ms,airtime_ms\n");
    for (auto& b : nodes) {
      for (auto& ev : b.node->deliveries) {
        fprintf(f, "%d,%u,%u,%u,%.2f,%llu,%u\n",
                ev.recv_node, ev.payload_type, ev.route_type,
                ev.hop_count, ev.snr,
                (unsigned long long)ev.rx_time_ms, ev.airtime_ms);
      }
    }
  }

protected:
  std::vector<InFlightPacket> _in_flight;
  std::map<uint32_t,int> _ts_to_seq;       // advert timestamp -> flood seq (single-sender; prefer _tsnode_to_seq)
  std::map<uint64_t,int> _tsnode_to_seq;  // (src_node<<32|advert_ts) -> flood seq (concurrent-safe)
  std::map<int,int>      _seq_to_src;      // flood seq -> src node_idx
  std::map<int,uint64_t> _inject_times;    // flood_seq -> inject time

  void onTransmit(int src, const uint8_t* bytes, int len, uint32_t airtime_ms, float tx_power_dbm) {
    if (len > 255) len = 255;  // LoRa hard cap; guard against oversized packets
    InFlightPacket ifp;
    ifp.src_node     = src;
    memcpy(ifp.bytes, bytes, len);
    ifp.len          = len;
    ifp.arrive_at_ms = clock.now() + propagation_delay_ms + airtime_ms;
    ifp.tx_power_dbm = tx_power_dbm;
    _in_flight.push_back(ifp);
  }

  void deliverPending() {
    uint64_t now = clock.now();
    auto it = _in_flight.begin();
    while (it != _in_flight.end()) {
      if (it->arrive_at_ms <= now) {
        // TX power offset: deviation from reference 20 dBm shifts received SNR linearly.
        float power_offset_db = it->tx_power_dbm - 20.0f;
        for (auto& b : nodes) {
          int dst = b.node->node_idx;
          if (dst == it->src_node) { continue; }
          if (!channel_model || channel_model->canReceive(it->src_node, dst)) {
            float snr = channel_model
                ? channel_model->receivedSNR(it->src_node, dst, 8.0f)
                : 8.0f;
            snr += power_offset_db;  // lower TX power → lower received SNR
            b.radio->injectRecv(it->bytes, it->len, snr, it->src_node);
          }
        }
        it = _in_flight.erase(it);
      } else {
        ++it;
      }
    }
  }
};

}
