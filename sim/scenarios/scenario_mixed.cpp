// scenario_mixed.cpp
// Tests networks where ADAPTIVE nodes coexist with legacy DEFAULT nodes.
//
// In real deployments the firmware upgrade is never 100% simultaneous.
// Key questions:
//   1. Does ADAPTIVE break routing for DEFAULT nodes? (compatibility)
//   2. At what ADAPTIVE penetration % does the network see measurable benefit?
//   3. Does ADAPTIVE unfairly starve DEFAULT nodes of relay slots?
//
// Test matrix:
//   ADAPTIVE penetration: 0%, 10%, 25%, 50%, 75%, 100%
//   Topologies: FullMesh 50, Grid 5×5, Chain 20
//   ADAPTIVE nodes placed randomly (uniform distribution)
//
// Baseline: 0% ADAPTIVE = pure DEFAULT (stock MeshCore behaviour)
// Red line: any config where delivery drops below the 0% baseline

#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

using namespace sim;

struct MixedResult {
    int num_nodes;
    int adaptive_pct;
    int adaptive_count;
    const char* topo;
    float avg_delivery_rate;
    float avg_latency_ms;
    float avg_hops;
    uint32_t total_collisions;
    uint64_t total_airtime_ms;
};

static MixedResult runMixed(
    RFChannelModel* model,
    const char* topo_name,
    int num_nodes, int grid_rows, int grid_cols,
    float snr, int adaptive_pct, int num_floods)
{
    SimBus bus;
    bus.tick_ms = 5;

    for (int i = 0; i < num_nodes; i++) {
        char name[32];
        if (grid_rows > 0)
            snprintf(name, sizeof(name), "n%d_%d", i/grid_cols, i%grid_cols);
        else
            snprintf(name, sizeof(name), "node%d", i);
        bus.addNode(name, (uint32_t)(i + 1) * 0xdeadbeef);
    }
    bus.channel_model = model;

    // Assign ADAPTIVE to a deterministic subset (every N-th node)
    // — deterministic placement, not random, for reproducibility
    int adaptive_count = (num_nodes * adaptive_pct + 50) / 100;
    for (int i = 0; i < num_nodes; i++) {
        // Evenly space ADAPTIVE nodes through the network
        bool is_adaptive = adaptive_count > 0 &&
                           (i * adaptive_count / num_nodes) <
                           ((i + 1) * adaptive_count / num_nodes);
        bus.nodes[i].node->routing_strategy =
            is_adaptive ? RoutingStrategy::ADAPTIVE : RoutingStrategy::DEFAULT;
    }

    // Warmup
    for (int i = 0; i < 3; i++) {
        bus.sendFloodText(0, "warmup");
        uint64_t wp = grid_rows > 0 ? 8000 : 3000;
        bus.run(wp);
    }
    bus.resetStats();

    uint64_t prop_ms = grid_rows > 0 ? 8000 : 5000;
    for (int i = 0; i < num_floods; i++) {
        bus.sendFloodText(0, "bench");
        bus.run(prop_ms);
    }

    auto stats = bus.metrics.aggregate(num_floods);
    uint64_t total_air = 0;
    for (auto& b : bus.nodes) total_air += b.node->total_airtime_ms;

    return {
        num_nodes, adaptive_pct, adaptive_count, topo_name,
        stats.avg_delivery_rate, stats.avg_latency_ms, stats.avg_hops,
        bus.totalCollisions(), total_air
    };
}

int main() {
    printf("MeshCore Mixed Firmware Compatibility Scenario\n");
    printf("===============================================\n");
    printf("ADAPTIVE nodes coexisting with legacy DEFAULT nodes\n\n");

    FILE* csv = fopen("mixed_results.csv", "w");
    if (csv)
        fprintf(csv, "topo,num_nodes,adaptive_pct,adaptive_count,"
                     "avg_delivery_rate,avg_latency_ms,avg_hops,"
                     "total_collisions,total_airtime_ms\n");

    struct Config {
        const char* name;
        int nodes, rows, cols;
        float snr;
    };
    Config configs[] = {
        { "FM50",   50,  0, 0, 8.0f },
        { "FM100", 100,  0, 0, 8.0f },
        { "GR5x5",  25,  5, 5, 8.0f },
        { "CH20",   20,  0, 0, 8.0f },  // chain — sparse, most sensitive to relay gaps
    };

    int pcts[] = { 0, 10, 25, 50, 75, 100 };

    for (auto& cfg : configs) {
        RFChannelModel* model = nullptr;
        if (cfg.rows > 0) {
            auto* pm = new PositionalModel(1.5f, cfg.snr + 4.f, cfg.snr);
            for (int r = 0; r < cfg.rows; r++)
                for (int c = 0; c < cfg.cols; c++)
                    pm->addNode((float)c, (float)r);
            model = pm;
        } else if (cfg.name[0] == 'C') {
            model = new ChainModel(cfg.snr);
        } else {
            model = new FullMeshModel(cfg.snr);
        }

        printf("\n--- %s (%d nodes) ---\n", cfg.name, cfg.nodes);
        printf("  %-8s %-7s | dr%%     lat(ms)  hops  coll  air(ms)      | Δdr    Δair\n",
               "ADAPT%", "n_adapt");

        MixedResult baseline{};
        bool have_base = false;

        for (int pct : pcts) {
            auto r = runMixed(model, cfg.name, cfg.nodes, cfg.rows, cfg.cols,
                              cfg.snr, pct, 20);

            if (!have_base) {
                baseline = r;
                have_base = true;
                printf("  %-8d %-7d | %5.1f%%  %7.0f  %4.1f  %-5u %12lld  (baseline)\n",
                       pct, r.adaptive_count,
                       r.avg_delivery_rate * 100.f, r.avg_latency_ms,
                       r.avg_hops, r.total_collisions,
                       (long long)r.total_airtime_ms);
            } else {
                float dr_d  = (r.avg_delivery_rate - baseline.avg_delivery_rate) * 100.f;
                float air_p = baseline.total_airtime_ms > 0
                    ? (float)((long long)r.total_airtime_ms - (long long)baseline.total_airtime_ms)
                      / (float)baseline.total_airtime_ms * 100.f : 0.f;
                const char* flag = (r.avg_delivery_rate < baseline.avg_delivery_rate - 0.02f)
                                   ? " !! REGRESSION" : "";
                printf("  %-8d %-7d | %5.1f%%  %7.0f  %4.1f  %-5u %12lld | %+5.1f%% %+5.0f%%%s\n",
                       pct, r.adaptive_count,
                       r.avg_delivery_rate * 100.f, r.avg_latency_ms,
                       r.avg_hops, r.total_collisions,
                       (long long)r.total_airtime_ms,
                       dr_d, air_p, flag);
            }

            if (csv)
                fprintf(csv, "%s,%d,%d,%d,%.4f,%.1f,%.2f,%u,%lld\n",
                        cfg.name, cfg.nodes, pct, r.adaptive_count,
                        r.avg_delivery_rate, r.avg_latency_ms, r.avg_hops,
                        r.total_collisions, (long long)r.total_airtime_ms);
        }

        delete model;
    }

    printf("\n=== KEY COMPATIBILITY CHECKS ===\n");
    printf("- Any REGRESSION flag = delivery dropped > 2%% vs all-DEFAULT baseline\n");
    printf("- Watch chain topology — sparse networks most sensitive to relay gaps\n");
    printf("- Airtime reduction at 100%% ADAPTIVE vs 0%% shows maximum efficiency gain\n");
    printf("\nCSV written to mixed_results.csv\n");

    if (csv) fclose(csv);
    return 0;
}
