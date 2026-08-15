// scenario_longchain.cpp
// Tests multi-hop relay performance at chain lengths targeting multi-state range.
//
// Real-world target: reliable delivery across hundreds of miles via relay chains.
// Each node in a chain extends range by ~2-5 miles (SF12, open terrain).
// A 200-node chain = ~400-1000 miles theoretical range.
//
// This scenario finds where delivery rate collapses, what the per-hop loss
// accumulation looks like, and how ADAPTIVE vs DEFAULT compares at depth.
//
// Chain lengths: 10, 20, 30, 50, 75, 100, 150, 200 nodes
// SNR conditions: 8 dB (good), 5 dB (marginal), 3 dB (poor)
// Strategies: DEFAULT, ADAPTIVE
// Floods: 20 per config (enough to average out random backoff variance)

#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

using namespace sim;

struct ChainResult {
    int num_nodes;
    float snr;
    RoutingStrategy strategy;
    float avg_delivery_rate;
    float avg_latency_ms;
    float avg_hops;
    uint32_t total_collisions;
    uint64_t total_airtime_ms;
};

static ChainResult runChain(int num_nodes, float snr, RoutingStrategy strat, int num_floods) {
    SimBus bus;
    bus.tick_ms = 5;

    auto* model = new ChainModel(snr);
    bus.channel_model = model;

    for (int i = 0; i < num_nodes; i++) {
        char name[32];
        snprintf(name, sizeof(name), "node%d", i);
        bus.addNode(name, (uint32_t)(i + 1) * 0xcafebabe);
    }

    for (auto& b : bus.nodes) b.node->routing_strategy = strat;

    // Warmup — chain needs more time to propagate
    uint64_t warmup_prop = (uint64_t)num_nodes * 400 + 2000;
    bus.sendFloodText(0, "warmup"); bus.run(warmup_prop);
    bus.sendFloodText(0, "warmup"); bus.run(warmup_prop);
    bus.resetStats();

    // Propagation budget scales with chain length
    // Each hop takes ~400-800ms under DEFAULT; give generous margin
    uint64_t prop_ms = (uint64_t)num_nodes * 600 + 3000;

    for (int i = 0; i < num_floods; i++) {
        bus.sendFloodText(0, "bench");
        bus.run(prop_ms);
    }

    auto stats = bus.metrics.aggregate(num_floods);
    uint64_t total_air = 0;
    for (auto& b : bus.nodes) total_air += b.node->total_airtime_ms;

    delete model;
    return {
        num_nodes, snr, strat,
        stats.avg_delivery_rate, stats.avg_latency_ms, stats.avg_hops,
        bus.totalCollisions(), total_air
    };
}

static const char* stratName(RoutingStrategy s) {
    return s == RoutingStrategy::DEFAULT  ? "DEFAULT " : "ADAPTIVE";
}

int main() {
    printf("MeshCore Long Relay Chain Scenario\n");
    printf("=====================================\n");
    printf("Target: multi-state range via relay chains\n");
    printf("Each node ≈ 2-5 miles range (SF12 LoRa, open terrain)\n\n");

    FILE* csv = fopen("longchain_results.csv", "w");
    if (csv)
        fprintf(csv, "num_nodes,snr,strategy,avg_delivery_rate,avg_latency_ms,"
                     "avg_hops,total_collisions,total_airtime_ms\n");

    int chain_lengths[] = { 10, 20, 30, 50, 75, 100, 150, 200 };
    float snr_vals[]    = { 8.0f, 5.0f, 3.0f };
    RoutingStrategy strats[] = { RoutingStrategy::DEFAULT, RoutingStrategy::ADAPTIVE };

    for (float snr : snr_vals) {
        printf("\n=== SNR = %.0f dB ===\n", snr);
        printf("%-6s %-8s | dr%%     lat(ms)  hops  coll  air(ms)      | Δdr    Δlat   Δair\n",
               "nodes", "strat");
        printf("%s\n", std::string(95, '-').c_str());

        for (int n : chain_lengths) {
            ChainResult def_r{};
            bool have_def = false;

            for (auto& strat : strats) {
                int floods = 20;
                // Longer chains need fewer floods to keep runtime reasonable
                if (n >= 100) floods = 10;
                if (n >= 150) floods = 5;

                auto r = runChain(n, snr, strat, floods);

                if (!have_def) {
                    def_r = r;
                    have_def = true;
                    printf("%-6d %-8s | %5.1f%%  %7.0f  %4.1f  %-5u %12lld\n",
                           r.num_nodes, stratName(r.strategy),
                           r.avg_delivery_rate * 100.f, r.avg_latency_ms,
                           r.avg_hops, r.total_collisions,
                           (long long)r.total_airtime_ms);
                } else {
                    float dr_d = (r.avg_delivery_rate - def_r.avg_delivery_rate) * 100.f;
                    float lat_p = def_r.avg_latency_ms > 0
                        ? (r.avg_latency_ms - def_r.avg_latency_ms) / def_r.avg_latency_ms * 100.f : 0.f;
                    float air_p = def_r.total_airtime_ms > 0
                        ? (float)((long long)r.total_airtime_ms - (long long)def_r.total_airtime_ms)
                          / (float)def_r.total_airtime_ms * 100.f : 0.f;
                    printf("%-6d %-8s | %5.1f%%  %7.0f  %4.1f  %-5u %12lld | %+5.1f%% %+6.0f%% %+6.0f%%\n",
                           r.num_nodes, stratName(r.strategy),
                           r.avg_delivery_rate * 100.f, r.avg_latency_ms,
                           r.avg_hops, r.total_collisions,
                           (long long)r.total_airtime_ms,
                           dr_d, lat_p, air_p);
                }

                if (csv)
                    fprintf(csv, "%d,%.1f,%s,%.4f,%.1f,%.2f,%u,%lld\n",
                            r.num_nodes, snr, stratName(r.strategy),
                            r.avg_delivery_rate, r.avg_latency_ms, r.avg_hops,
                            r.total_collisions, (long long)r.total_airtime_ms);
            }
        }
    }

    printf("\n=== RANGE ESTIMATE (@ 3 miles/hop, SF12) ===\n");
    printf("%-10s  %-12s  %s\n", "Nodes", "Est. Range", "Notes");
    int sizes[] = {10,20,30,50,75,100,150,200};
    for (int n : sizes)
        printf("  %-8d  ~%-10s  %s\n", n,
               n<=10?"30 miles":n<=20?"60 miles":n<=30?"90 miles":
               n<=50?"150 miles":n<=75?"225 miles":n<=100?"300 miles":
               n<=150?"450 miles":"600 miles",
               n<=20?"city-scale":n<=50?"regional":n<=100?"state-scale":"multi-state");

    if (csv) fclose(csv);
    printf("\nCSV written to longchain_results.csv\n");
    return 0;
}
