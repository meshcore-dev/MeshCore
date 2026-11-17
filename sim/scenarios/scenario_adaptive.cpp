// scenario_adaptive.cpp
// Validates the ADAPTIVE density-aware routing strategy against DEFAULT and
// PATH_SNR_HYBRID baselines.
//
// Test matrix:
//   Topologies: FullMesh (dense), Chain (sparse), Grid (mixed)
//   Node counts: 10 (sparse), 25 (medium), 50 (dense), 100 (very dense)
//   SNR: 8dB nominal, 3dB stress
//   Strategies: DEFAULT, PATH_SNR_HYBRID, ADAPTIVE
//
// Expected: ADAPTIVE matches or beats DEFAULT on delivery,
//           beats HYBRID on airtime in dense configs,
//           beats DEFAULT on latency in sparse/chain configs.

#include "TestRunner.h"
#include <cstdio>
#include <vector>

using namespace sim;

int main() {
    printf("MeshCore ADAPTIVE Strategy Validation\n");
    printf("======================================\n\n");

    FILE* csv = fopen("adaptive_results.csv", "w");
    if (csv) {
        fprintf(csv, "label,topo,nodes,snr,strategy,"
                     "avg_delivery_rate,avg_latency_ms,avg_hops,total_airtime_ms\n");
    }

    auto csv_row = [&](const char* label, const TestResult& r) {
        if (!csv) return;
        const char* topo = r.tc.topo == TestCase::TopoType::FULL_MESH ? "FullMesh" :
                           r.tc.topo == TestCase::TopoType::CHAIN      ? "Chain"    : "Grid";
        const char* strat = r.tc.strategy == RoutingStrategy::DEFAULT        ? "DEFAULT"        :
                            r.tc.strategy == RoutingStrategy::PATH_SNR_HYBRID ? "PATH_SNR_HYBRID" :
                            r.tc.strategy == RoutingStrategy::ADAPTIVE        ? "ADAPTIVE"        :
                                                                                "SNR_WEIGHTED";
        fprintf(csv, "%s,%s,%d,%.1f,%s,%.4f,%.2f,%.3f,%lld\n",
                label, topo, r.tc.num_nodes, r.tc.channel_snr, strat,
                r.stats.avg_delivery_rate, r.stats.avg_latency_ms,
                r.stats.avg_hops, (long long)r.stats.total_airtime_ms);
    };

    TestRunner runner;

    // -----------------------------------------------------------------------
    // Helper: print a result row + delta vs baseline
    // -----------------------------------------------------------------------
    auto print_row = [](const char* label, const TestResult& r,
                        const TestResult* base = nullptr) {
        const char* topo = r.tc.topo == TestCase::TopoType::FULL_MESH ? "FullMesh" :
                           r.tc.topo == TestCase::TopoType::CHAIN      ? "Chain"    : "Grid";
        const char* strat = r.tc.strategy == RoutingStrategy::DEFAULT         ? "DEFAULT   " :
                            r.tc.strategy == RoutingStrategy::PATH_SNR_HYBRID  ? "HYBRID    " :
                            r.tc.strategy == RoutingStrategy::ADAPTIVE         ? "ADAPTIVE  " :
                                                                                  "SNR_W     ";
        printf("  %-28s | strat=%s | topo=%-8s | n=%-3d | snr=%+4.0f | dr=%5.1f%% | lat=%6.0fms | air=%7lldms",
               label, strat, topo, r.tc.num_nodes, r.tc.channel_snr,
               r.stats.avg_delivery_rate * 100.0f,
               r.stats.avg_latency_ms,
               (long long)r.stats.total_airtime_ms);
        if (base) {
            float dr_d = (r.stats.avg_delivery_rate - base->stats.avg_delivery_rate) * 100.0f;
            float lat_pct = base->stats.avg_latency_ms > 0
                ? (r.stats.avg_latency_ms - base->stats.avg_latency_ms)
                  / base->stats.avg_latency_ms * 100.0f : 0.0f;
            float air_pct = base->stats.total_airtime_ms > 0
                ? (float)((long long)r.stats.total_airtime_ms - (long long)base->stats.total_airtime_ms)
                  / (float)base->stats.total_airtime_ms * 100.0f : 0.0f;
            printf("  Δdr=%+4.1f%% Δlat=%+5.0f%% Δair=%+5.0f%%",
                   dr_d, lat_pct, air_pct);
        }
        printf("\n");
    };

    // -----------------------------------------------------------------------
    // Test block: run DEFAULT, HYBRID, ADAPTIVE for a given config
    // -----------------------------------------------------------------------
    struct Config {
        TestCase::TopoType topo;
        int nodes, rows, cols;
        float snr;
        const char* tag;
    };

    Config configs[] = {
        // FullMesh — where density detection matters most
        { TestCase::TopoType::FULL_MESH,  10, 0, 0,  8.0f, "FM10_snr8"   },
        { TestCase::TopoType::FULL_MESH,  25, 0, 0,  8.0f, "FM25_snr8"   },
        { TestCase::TopoType::FULL_MESH,  50, 0, 0,  8.0f, "FM50_snr8"   },
        { TestCase::TopoType::FULL_MESH, 100, 0, 0,  8.0f, "FM100_snr8"  },
        // FullMesh at poor SNR — density still dense, but collisions compound
        { TestCase::TopoType::FULL_MESH,  50, 0, 0,  3.0f, "FM50_snr3"   },
        // Chain — sparse; ADAPTIVE should promote SNR_WEIGHTED
        { TestCase::TopoType::CHAIN,      10, 0, 0,  8.0f, "CH10_snr8"   },
        { TestCase::TopoType::CHAIN,      20, 0, 0,  8.0f, "CH20_snr8"   },
        { TestCase::TopoType::CHAIN,      20, 0, 0,  3.0f, "CH20_snr3"   },
        // Grid — medium density; ADAPTIVE should hit MEDIUM tier
        { TestCase::TopoType::GRID,       16, 4, 4,  8.0f, "GR4x4_snr8"  },
        { TestCase::TopoType::GRID,       25, 5, 5,  8.0f, "GR5x5_snr8"  },
        { TestCase::TopoType::GRID,       25, 5, 5,  6.0f, "GR5x5_snr6"  },
    };

    RoutingStrategy strats[] = {
        RoutingStrategy::DEFAULT,
        RoutingStrategy::PATH_SNR_HYBRID,
        RoutingStrategy::ADAPTIVE,
    };

    for (auto& cfg : configs) {
    printf("\n--- %s (topo=%s nodes=%d snr=%.0f) ---\n",
               cfg.tag,
               cfg.topo == TestCase::TopoType::FULL_MESH ? "FullMesh" :
               cfg.topo == TestCase::TopoType::CHAIN      ? "Chain"    : "Grid",
               cfg.nodes, cfg.snr);

        TestResult base_result{};
        bool have_base = false;
        std::vector<TestResult> group;

        for (auto& strat : strats) {
            TestCase tc{};
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "%s_%s", cfg.tag,
                     strat == RoutingStrategy::DEFAULT        ? "DEFAULT" :
                     strat == RoutingStrategy::PATH_SNR_HYBRID ? "HYBRID"  : "ADAPTIVE");
            tc.name       = lbl;
            tc.num_nodes  = cfg.nodes;
            tc.channel_snr = cfg.snr;
            tc.num_floods = 20;
            tc.strategy   = strat;
            tc.topo       = cfg.topo;
            tc.grid_rows  = cfg.rows;
            tc.grid_cols  = cfg.cols;

            auto result = runner.run({tc})[0];
            group.push_back(result);

            if (strat == RoutingStrategy::DEFAULT) {
                base_result = result;
                have_base = true;
                print_row(lbl, result);
            } else {
                print_row(lbl, result, have_base ? &base_result : nullptr);
            }
            csv_row(lbl, result);
        }
    }

    // -----------------------------------------------------------------------
    // Summary: how does ADAPTIVE perform vs DEFAULT across all configs?
    // -----------------------------------------------------------------------
    printf("\n======================================\n");
    printf("  ADAPTIVE vs DEFAULT — aggregate\n");
    printf("  See adaptive_results.csv for full data\n");
    printf("======================================\n");

    if (csv) {
        fclose(csv);
        printf("CSV written to adaptive_results.csv\n");
    }
    return 0;
}
