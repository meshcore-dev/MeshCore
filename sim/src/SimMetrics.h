#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <numeric>

namespace sim {

// Per-flood summary: one entry per packet injected into the network.
struct FloodSummary {
  int     src_node;
  int     flood_seq;         // injection sequence number
  uint64_t inject_time_ms;
  int     num_nodes;         // total nodes in sim
  int     num_received;      // how many other nodes received it
  float   delivery_rate;     // num_received / (num_nodes - 1)
  uint32_t min_latency_ms;
  uint32_t max_latency_ms;
  float    avg_latency_ms;
  float    min_hop;
  float    max_hop;
  float    avg_hop;
  uint64_t total_airtime_ms; // cumulative airtime across all relays for this flood
};

// Aggregate stats across all floods in a scenario run.
struct ScenarioStats {
  int num_floods;
  float avg_delivery_rate;
  float avg_latency_ms;
  float avg_hops;
  uint64_t total_airtime_ms;
  uint32_t total_tx;
  uint32_t total_rx;
};

// -------------------------------------------------------------------------
// SimMetrics — collects per-injection delivery records and produces reports.
// Attach to a SimBus by tracking flood injection times externally, then
// call record() when a node reports receipt.
// -------------------------------------------------------------------------
class SimMetrics {
public:
  struct Receipt {
    int     src_node;
    int     recv_node;
    int     flood_seq;
    uint64_t inject_ms;
    uint64_t recv_ms;
    uint32_t latency_ms;
    uint8_t  hop_count;
    uint8_t  route_type;
    float    snr;
    uint32_t airtime_ms;
  };

  std::vector<Receipt> receipts;
  int _num_nodes = 0;

  void setNumNodes(int n) { _num_nodes = n; }

  void record(int src, int recv, int seq, uint64_t inject_ms, uint64_t recv_ms,
              uint8_t hops, uint8_t route, float snr, uint32_t airtime_ms) {
    Receipt r;
    r.src_node    = src;
    r.recv_node   = recv;
    r.flood_seq   = seq;
    r.inject_ms   = inject_ms;
    r.recv_ms     = recv_ms;
    r.latency_ms  = (uint32_t)(recv_ms > inject_ms ? recv_ms - inject_ms : 0);
    r.hop_count   = hops;
    r.route_type  = route;
    r.snr         = snr;
    r.airtime_ms  = airtime_ms;
    receipts.push_back(r);
  }

  void reset() { receipts.clear(); }

  // -----------------------------------------------------------------------
  // Per-flood breakdown
  // -----------------------------------------------------------------------
  std::vector<FloodSummary> buildFloodSummaries(int num_floods) const {
    std::vector<FloodSummary> summaries;
    for (int seq = 0; seq < num_floods; seq++) {
      FloodSummary fs{};
      fs.flood_seq = seq;
      fs.num_nodes = _num_nodes;
      fs.min_latency_ms = UINT32_MAX;
      fs.min_hop = 255;

      std::vector<uint32_t> lats;
      std::vector<uint8_t> hops;
      uint64_t airtime_sum = 0;

      for (auto& r : receipts) {
        if (r.flood_seq != seq) continue;
        if (fs.inject_time_ms == 0) { fs.inject_time_ms = r.inject_ms; fs.src_node = r.src_node; }
        lats.push_back(r.latency_ms);
        hops.push_back(r.hop_count);
        airtime_sum += r.airtime_ms;
        fs.num_received++;
        if (r.latency_ms < fs.min_latency_ms) fs.min_latency_ms = r.latency_ms;
        if (r.latency_ms > fs.max_latency_ms) fs.max_latency_ms = r.latency_ms;
        if (r.hop_count < fs.min_hop) fs.min_hop = r.hop_count;
        if (r.hop_count > fs.max_hop) fs.max_hop = r.hop_count;
      }

      int possible = _num_nodes - 1;
      fs.delivery_rate = possible > 0 ? (float)fs.num_received / (float)possible : 0.0f;
      fs.total_airtime_ms = airtime_sum;

      if (!lats.empty()) {
        fs.avg_latency_ms = (float)std::accumulate(lats.begin(), lats.end(), 0ULL) / lats.size();
        fs.avg_hop = (float)std::accumulate(hops.begin(), hops.end(), 0) / hops.size();
      }
      if (fs.min_latency_ms == UINT32_MAX) fs.min_latency_ms = 0;

      summaries.push_back(fs);
    }
    return summaries;
  }

  ScenarioStats aggregate(int num_floods) const {
    auto summaries = buildFloodSummaries(num_floods);
    ScenarioStats s{};
    s.num_floods = num_floods;
    if (summaries.empty()) return s;

    float dr_sum = 0, lat_sum = 0, hop_sum = 0;
    for (auto& fs : summaries) {
      dr_sum  += fs.delivery_rate;
      lat_sum += fs.avg_latency_ms;
      hop_sum += fs.avg_hop;
      s.total_airtime_ms += fs.total_airtime_ms;
    }
    s.avg_delivery_rate = dr_sum / summaries.size();
    s.avg_latency_ms    = lat_sum / summaries.size();
    s.avg_hops          = hop_sum / summaries.size();

    for (auto& r : receipts) {
      s.total_rx++;
    }
    return s;
  }

  // -----------------------------------------------------------------------
  // Print human-readable table
  // -----------------------------------------------------------------------
  void printReport(const char* label, int num_floods) const {
    auto s = aggregate(num_floods);
    printf("\n=== Metrics: %s ===\n", label);
    printf("  Floods injected:     %d\n", num_floods);
    printf("  Avg delivery rate:   %.1f%%\n", s.avg_delivery_rate * 100.0f);
    printf("  Avg latency:         %.0f ms\n", s.avg_latency_ms);
    printf("  Avg hops:            %.1f\n", s.avg_hops);
    printf("  Total airtime:       %llu ms\n", (unsigned long long)s.total_airtime_ms);
    printf("\n  Per-flood breakdown:\n");
    printf("  %-6s %-8s %-12s %-12s %-8s %-10s\n",
           "Seq", "Recvd", "DelivRate%", "AvgLat(ms)", "AvgHop", "Air(ms)");
    for (auto& fs : buildFloodSummaries(num_floods)) {
      printf("  %-6d %-8d %-12.1f %-12.0f %-8.1f %-10llu\n",
             fs.flood_seq, fs.num_received, fs.delivery_rate * 100.0f,
             fs.avg_latency_ms, fs.avg_hop,
             (unsigned long long)fs.total_airtime_ms);
    }
    printf("=================================\n");
  }

  // -----------------------------------------------------------------------
  // Dump CSV for external analysis
  // -----------------------------------------------------------------------
  void dumpCSV(FILE* f = stdout) const {
    fprintf(f, "flood_seq,src_node,recv_node,latency_ms,hop_count,route_type,snr,airtime_ms\n");
    for (auto& r : receipts) {
      fprintf(f, "%d,%d,%d,%u,%u,%u,%.2f,%u\n",
              r.flood_seq, r.src_node, r.recv_node,
              r.latency_ms, r.hop_count, r.route_type,
              r.snr, r.airtime_ms);
    }
  }
};

}
