#include "SimBus.h"
#include "TestRunner.h"
#include <cstdio>
#include <vector>

// -------------------------------------------------------------------------
// scenario_routing_comparison
//
// Sweeps all three routing strategies across FullMesh, Chain, and Grid
// topologies at multiple node counts and prints RESULT / DELTA tables.
// -------------------------------------------------------------------------

using sim::TestCase;
using sim::RoutingStrategy;

static std::vector<TestCase> buildCases() {
  std::vector<TestCase> cases;

  const float SNR = 8.0f;
  const int FLOODS = 20;

  // Helper to generate all three strategies for one topology config
  auto add = [&](const char* tag,
                 TestCase::TopoType topo,
                 int nodes,
                 int rows, int cols,
                 float snr) {
    for (auto strat : { RoutingStrategy::DEFAULT,
                        RoutingStrategy::SNR_WEIGHTED,
                        RoutingStrategy::PATH_SNR_HYBRID }) {
      TestCase tc;
      char buf[64];
      const char* sname =
        strat == RoutingStrategy::DEFAULT        ? "DEF" :
        strat == RoutingStrategy::SNR_WEIGHTED   ? "SNR" : "HYB";
      snprintf(buf, sizeof(buf), "%s_%s_n%d", tag, sname, nodes);
      tc.name       = buf;
      tc.topo       = topo;
      tc.num_nodes  = nodes;
      tc.channel_snr = snr;
      tc.num_floods = FLOODS;
      tc.strategy   = strat;
      tc.grid_rows  = rows;
      tc.grid_cols  = cols;
      cases.push_back(tc);
    }
  };

  // FullMesh: 5, 10, 20 nodes
  add("FM",  TestCase::TopoType::FULL_MESH,  5, 0, 0, SNR);
  add("FM",  TestCase::TopoType::FULL_MESH, 10, 0, 0, SNR);
  add("FM",  TestCase::TopoType::FULL_MESH, 20, 0, 0, SNR);

  // Chain: 5, 10, 20 nodes
  add("CH",  TestCase::TopoType::CHAIN,  5, 0, 0, SNR);
  add("CH",  TestCase::TopoType::CHAIN, 10, 0, 0, SNR);
  add("CH",  TestCase::TopoType::CHAIN, 20, 0, 0, SNR);

  // Grid: 3x3 (9), 4x4 (16), 5x5 (25)
  add("GR",  TestCase::TopoType::GRID,  9, 3, 3, SNR);
  add("GR",  TestCase::TopoType::GRID, 16, 4, 4, SNR);
  add("GR",  TestCase::TopoType::GRID, 25, 5, 5, SNR);

  return cases;
}

int main() {
  printf("=======================================================\n");
  printf("  MeshCore Routing Strategy Comparison\n");
  printf("  Strategies: DEFAULT | SNR_WEIGHTED | PATH_SNR_HYBRID\n");
  printf("  Floods per run: 20\n");
  printf("=======================================================\n\n");

  auto cases = buildCases();

  sim::TestRunner runner;
  auto results = runner.run(cases);

  runner.printComparison(results);

  // Also dump CSV for external analysis
  runner.dumpCSV(results, "routing_comparison.csv");

  printf("\nDone.\n");
  return 0;
}
