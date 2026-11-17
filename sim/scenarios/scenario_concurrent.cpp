// scenario_concurrent.cpp
// Tests collision behaviour under simultaneous flood sources.
//
// Real deployments have many independent senders — emergency beacons,
// tracking pings, chat messages — all launching floods at overlapping times.
// This scenario measures how delivery rate degrades as concurrent flood
// count increases, and how ADAPTIVE compares to DEFAULT under that load.
//
// Test matrix:
//   Concurrent senders: 1, 2, 4, 8  (launched simultaneously)
//   Topologies: FullMesh 50, FullMesh 100, Grid 5×5
//   Strategies: DEFAULT, ADAPTIVE
//
// Metrics: per-flood delivery rate, total collisions, total airtime.
// A collision at any node counts once — total_collisions gives channel load.

#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>

using namespace sim;

struct ConcurrentResult {
    int num_nodes;
    int concurrent_senders;
    RoutingStrategy strategy;
    const char* topo_name;
    float avg_delivery_rate;
    float avg_latency_ms;
    uint32_t total_collisions;
    uint64_t total_airtime_ms;
    uint32_t total_tx;
};

static ConcurrentResult runConcurrent(
    RFChannelModel* model,
    const char* topo_name,
    int num_nodes,
    int grid_rows, int grid_cols,
    float snr,
    int concurrent_senders,
    int rounds,           // how many rounds of concurrent floods
    RoutingStrategy strat)
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

    for (auto& b : bus.nodes) b.node->routing_strategy = strat;

    // Warmup: prime density estimators
    for (int i = 0; i < 3; i++) {
        bus.sendFloodText(0, "warmup");
        bus.run(4000);
    }
    bus.resetStats();

    // Each round: launch `concurrent_senders` floods simultaneously
    // from evenly-spaced nodes, then let them fully propagate
    int total_floods = 0;
    uint64_t prop_ms = (grid_rows > 0) ? 10000 : 5000;

    for (int round = 0; round < rounds; round++) {
        for (int s = 0; s < concurrent_senders && s < num_nodes; s++) {
            int src = (s * num_nodes) / concurrent_senders;  // evenly spaced
            bus.sendFloodText(src, "concurrent");
            total_floods++;
        }
        bus.run(prop_ms);
    }

    auto stats = bus.metrics.aggregate(total_floods);
    uint64_t total_air = 0;
    uint32_t total_tx = 0;
    for (auto& b : bus.nodes) {
        total_air += b.node->total_airtime_ms;
        total_tx  += b.node->total_tx_packets;
    }

    return {
        num_nodes, concurrent_senders, strat, topo_name,
        stats.avg_delivery_rate, stats.avg_latency_ms,
        bus.totalCollisions(), total_air, total_tx
    };
}

static const char* stratName(RoutingStrategy s) {
    return s == RoutingStrategy::DEFAULT  ? "DEFAULT " : "ADAPTIVE";
}

int main() {
    printf("MeshCore Concurrent Flood Scenario\n");
    printf("====================================\n");
    printf("Collision model: LoRa capture effect (6 dB threshold)\n");
    printf("Metric: delivery rate, collisions, airtime under simultaneous sources\n\n");

    FILE* csv = fopen("concurrent_results.csv", "w");
    if (csv)
        fprintf(csv, "topo,num_nodes,concurrent_senders,strategy,"
                     "avg_delivery_rate,avg_latency_ms,total_collisions,total_airtime_ms,total_tx\n");

    auto print_row = [&](const ConcurrentResult& r, const ConcurrentResult* base = nullptr) {
        printf("  %-8s n=%-3d senders=%-2d strat=%s | dr=%5.1f%% lat=%6.0fms "
               "coll=%-6u air=%8lldms tx=%-5u",
               r.topo_name, r.num_nodes, r.concurrent_senders, stratName(r.strategy),
               r.avg_delivery_rate * 100.f, r.avg_latency_ms,
               r.total_collisions, (long long)r.total_airtime_ms, r.total_tx);
        if (base) {
            float dr_d  = (r.avg_delivery_rate - base->avg_delivery_rate) * 100.f;
            float air_p = base->total_airtime_ms > 0
                ? (float)((long long)r.total_airtime_ms - (long long)base->total_airtime_ms)
                  / (float)base->total_airtime_ms * 100.f : 0.f;
            int   cd    = (int)r.total_collisions - (int)base->total_collisions;
            printf("  Δdr=%+4.1f%% Δair=%+5.0f%% Δcoll=%+d", dr_d, air_p, cd);
        }
        printf("\n");
        if (csv)
            fprintf(csv, "%s,%d,%d,%s,%.4f,%.1f,%u,%lld,%u\n",
                    r.topo_name, r.num_nodes, r.concurrent_senders, stratName(r.strategy),
                    r.avg_delivery_rate, r.avg_latency_ms,
                    r.total_collisions, (long long)r.total_airtime_ms, r.total_tx);
    };

    struct Config {
        const char* name;
        int nodes, rows, cols;
        float snr;
    };
    Config configs[] = {
        { "FM50",    50,  0, 0, 8.0f },
        { "FM100",  100,  0, 0, 8.0f },
        { "GR5x5",   25,  5, 5, 8.0f },
    };

    int sender_counts[] = { 1, 2, 4, 8 };
    RoutingStrategy strats[] = { RoutingStrategy::DEFAULT, RoutingStrategy::ADAPTIVE };

    for (auto& cfg : configs) {
        RFChannelModel* model = nullptr;
        if (cfg.rows > 0) {
            auto* pm = new PositionalModel(1.5f, cfg.snr + 4.f, cfg.snr);
            for (int r = 0; r < cfg.rows; r++)
                for (int c = 0; c < cfg.cols; c++)
                    pm->addNode((float)c, (float)r);
            model = pm;
        } else {
            model = new FullMeshModel(cfg.snr);
        }

        printf("\n--- Topology: %s (%d nodes, SNR=%.0fdB) ---\n",
               cfg.name, cfg.nodes, cfg.snr);

        for (int ns : sender_counts) {
            if (ns >= cfg.nodes) continue;
            ConcurrentResult def_base{};
            bool have_def = false;

            for (auto& strat : strats) {
                auto r = runConcurrent(model, cfg.name, cfg.nodes,
                                       cfg.rows, cfg.cols, cfg.snr,
                                       ns, 10, strat);
                if (!have_def) {
                    def_base = r; have_def = true;
                    print_row(r);
                } else {
                    print_row(r, &def_base);
                }
            }
            printf("\n");
        }

        delete model;
    }

    // Summary: find the sender count where delivery drops below 90%
    printf("\n=== COLLISION SENSITIVITY SUMMARY ===\n");
    printf("At what concurrent sender count does delivery first drop below 90%%?\n");
    printf("(See concurrent_results.csv for full data)\n");

    if (csv) fclose(csv);
    return 0;
}
