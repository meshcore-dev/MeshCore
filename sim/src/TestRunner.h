#pragma once
#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

namespace sim {

// -------------------------------------------------------------------------
// TestCase — one parameterised run of the simulation.
// -------------------------------------------------------------------------
struct TestCase {
  std::string name;
  int num_nodes;
  float channel_snr;   // uniform SNR for FullMesh / Chain models
  int num_floods;
  RoutingStrategy strategy;

  enum class TopoType { FULL_MESH, CHAIN, GRID } topo;
  int grid_rows = 0;
  int grid_cols = 0;   // num_nodes = grid_rows * grid_cols for GRID
};

// -------------------------------------------------------------------------
// TestResult — collected stats for one TestCase.
// -------------------------------------------------------------------------
struct TestResult {
  TestCase tc;
  ScenarioStats stats;
};

// -------------------------------------------------------------------------
// TestRunner — runs a batch of TestCases and produces comparison tables.
// -------------------------------------------------------------------------
class TestRunner {
public:
  // -----------------------------------------------------------------------
  // Run all test cases, return results in order.
  // -----------------------------------------------------------------------
  std::vector<TestResult> run(const std::vector<TestCase>& cases) {
    std::vector<TestResult> results;
    results.reserve(cases.size());

    for (auto& tc : cases) {
      results.push_back(runOne(tc));
    }
    return results;
  }

  // -----------------------------------------------------------------------
  // Print RESULT lines + DELTA table relative to DEFAULT strategy.
  // -----------------------------------------------------------------------
  void printComparison(const std::vector<TestResult>& results) {
    printf("\n");
    // -- RESULT lines -------------------------------------------------------
    for (auto& r : results) {
      printf("RESULT | strategy=%-16s | topo=%-8s | nodes=%-3d | dr=%5.1f%% | lat=%6.0fms | hops=%4.1f | air=%lldms\n",
             strategyName(r.tc.strategy),
             topoName(r.tc.topo),
             r.tc.num_nodes,
             r.stats.avg_delivery_rate * 100.0f,
             r.stats.avg_latency_ms,
             r.stats.avg_hops,
             (long long)r.stats.total_airtime_ms);
    }

    printf("\n");

    // -- DELTA lines — compare SNR_WEIGHTED and PATH_SNR_HYBRID vs DEFAULT --
    // Find DEFAULT baseline for each (topo, num_nodes) pair
    for (auto& r : results) {
      if (r.tc.strategy == RoutingStrategy::DEFAULT) continue;

      // Find matching DEFAULT
      const TestResult* base = nullptr;
      for (auto& b : results) {
        if (b.tc.strategy == RoutingStrategy::DEFAULT &&
            b.tc.topo     == r.tc.topo &&
            b.tc.num_nodes == r.tc.num_nodes) {
          base = &b;
          break;
        }
      }
      if (!base) continue;

      float dr_delta  = (r.stats.avg_delivery_rate - base->stats.avg_delivery_rate) * 100.0f;
      float lat_pct   = base->stats.avg_latency_ms  > 0.0f
                        ? (r.stats.avg_latency_ms - base->stats.avg_latency_ms) / base->stats.avg_latency_ms * 100.0f
                        : 0.0f;
      float hop_delta = r.stats.avg_hops - base->stats.avg_hops;
      float air_pct   = base->stats.total_airtime_ms > 0
                        ? (float)((long long)r.stats.total_airtime_ms - (long long)base->stats.total_airtime_ms)
                          / (float)base->stats.total_airtime_ms * 100.0f
                        : 0.0f;

      printf("DELTA  | strategy=%-16s | topo=%-8s | nodes=%-3d | dr=%+5.1f%% | lat=%+6.0f%% | hops=%+4.1f | air=%+5.0f%%\n",
             strategyName(r.tc.strategy),
             topoName(r.tc.topo),
             r.tc.num_nodes,
             dr_delta,
             lat_pct,
             hop_delta,
             air_pct);
    }
  }

  // -----------------------------------------------------------------------
  // Dump all results as CSV to a file.
  // -----------------------------------------------------------------------
  void dumpCSV(const std::vector<TestResult>& results, const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
      printf("ERROR: cannot open %s for writing\n", filename);
      return;
    }
    fprintf(f, "name,topo,num_nodes,channel_snr,num_floods,strategy,"
               "avg_delivery_rate,avg_latency_ms,avg_hops,total_airtime_ms\n");
    for (auto& r : results) {
      fprintf(f, "%s,%s,%d,%.2f,%d,%s,%.4f,%.2f,%.3f,%lld\n",
              r.tc.name.c_str(),
              topoName(r.tc.topo),
              r.tc.num_nodes,
              r.tc.channel_snr,
              r.tc.num_floods,
              strategyName(r.tc.strategy),
              r.stats.avg_delivery_rate,
              r.stats.avg_latency_ms,
              r.stats.avg_hops,
              (long long)r.stats.total_airtime_ms);
    }
    fclose(f);
    printf("CSV written to %s\n", filename);
  }

private:
  // -----------------------------------------------------------------------
  // Run a single TestCase.
  // -----------------------------------------------------------------------
  TestResult runOne(const TestCase& tc) {
    SimBus bus;
    bus.tick_ms = 5;

    char name_buf[32];

    if (tc.topo == TestCase::TopoType::GRID) {
      // Grid: PositionalModel, nodes at integer grid positions
      // Spacing=1.0, range=1.5 so orthogonal neighbours connect, diagonals don't
      auto* model = new PositionalModel(1.5f, (float)tc.channel_snr + 4.0f, tc.channel_snr);
      bus.channel_model = model;

      int rows = tc.grid_rows;
      int cols = tc.grid_cols;
      for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
          snprintf(name_buf, sizeof(name_buf), "n%d_%d", r, c);
          bus.addNode(name_buf, (uint32_t)(r * cols + c + 1) * 0x1337cafe);
          model->addNode((float)c, (float)r);
        }
      }
      _owned_channel = std::unique_ptr<RFChannelModel>(model);
    } else if (tc.topo == TestCase::TopoType::CHAIN) {
      auto* model = new ChainModel(tc.channel_snr);
      bus.channel_model = model;
      _owned_channel = std::unique_ptr<RFChannelModel>(model);

      for (int i = 0; i < tc.num_nodes; i++) {
        snprintf(name_buf, sizeof(name_buf), "node%d", i);
        bus.addNode(name_buf, (uint32_t)(i + 1) * 0xcafebabe);
      }
    } else {
      // FULL_MESH
      auto* model = new FullMeshModel(tc.channel_snr);
      bus.channel_model = model;
      _owned_channel = std::unique_ptr<RFChannelModel>(model);

      for (int i = 0; i < tc.num_nodes; i++) {
        snprintf(name_buf, sizeof(name_buf), "node%d", i);
        bus.addNode(name_buf, (uint32_t)(i + 1) * 0xdeadbeef);
      }
    }

    // Apply routing strategy to all nodes
    for (auto& b : bus.nodes) {
      b.node->routing_strategy = tc.strategy;
    }

    // Warmup — run time + a few probe floods so density estimators
    // have real neighbor observations before measurement begins.
    bus.run(2000);
    for (int i = 0; i < 3; i++) {
      bus.sendFloodText(0, "warmup");
      uint64_t wp = (tc.topo == TestCase::TopoType::CHAIN) ? 6000 :
                    (tc.topo == TestCase::TopoType::GRID)  ? 8000 : 3000;
      bus.run(wp);
    }
    bus.resetStats();  // clear warmup metrics; density estimator retains observations

    // Choose per-topo propagation budget
    uint64_t prop_ms = 3000;
    if (tc.topo == TestCase::TopoType::CHAIN) prop_ms = 6000;
    if (tc.topo == TestCase::TopoType::GRID)  prop_ms = 8000;

    // Inject floods from node 0
    for (int i = 0; i < tc.num_floods; i++) {
      bus.sendFloodText(0, "bench");
      bus.run(prop_ms);
    }

    TestResult tr;
    tr.tc    = tc;
    tr.stats = bus.metrics.aggregate(tc.num_floods);

    // Accumulate total TX airtime across all nodes
    uint64_t total_air = 0;
    for (auto& b : bus.nodes) total_air += b.node->total_airtime_ms;
    tr.stats.total_airtime_ms = total_air;
    tr.stats.total_tx = 0;
    for (auto& b : bus.nodes) tr.stats.total_tx += b.node->total_tx_packets;

    return tr;
  }

  static const char* strategyName(RoutingStrategy s) {
    switch (s) {
      case RoutingStrategy::DEFAULT:        return "DEFAULT";
      case RoutingStrategy::SNR_WEIGHTED:   return "SNR_WEIGHTED";
      case RoutingStrategy::PATH_SNR_HYBRID:return "PATH_SNR_HYBRID";
      case RoutingStrategy::ADAPTIVE:       return "ADAPTIVE";
      default:                              return "UNKNOWN";
    }
  }

  static const char* topoName(TestCase::TopoType t) {
    switch (t) {
      case TestCase::TopoType::FULL_MESH: return "FullMesh";
      case TestCase::TopoType::CHAIN:     return "Chain";
      case TestCase::TopoType::GRID:      return "Grid";
      default:                            return "Unknown";
    }
  }

  // Channel model lifetime management for runOne
  std::unique_ptr<RFChannelModel> _owned_channel;
};

}  // namespace sim
