// scenario_dutycycle.cpp
// Models duty cycle budget enforcement and its effect on delivery.
//
// The EU 1% duty cycle rule allows each node ~864 ms TX per hour (~36s/day).
// At LoRa SF8 BW62.5kHz, a typical 50-byte flood packet is ~623ms airtime.
// That means a dense node can legally relay only ~1-2 floods per hour before
// going silent. In a network with many nodes and many floods, this is the
// binding constraint — not SNR, not hops.
//
// This scenario answers:
//   1. At what flood rate does the 1% limit start silencing nodes?
//   2. Does ADAPTIVE's reduced TX (10-20% of nodes relay) help stay legal?
//   3. What is the delivery impact when nodes hit budget and go silent?
//   4. Which topology is most resilient to node silence?
//
// Budget factors tested:
//   factor=1   → 50% (stock, no real enforcement)
//   factor=9   → 10% (typical non-EU, relaxed)
//   factor=99  → 1%  (EU legal, strictest)
//
// Flood rates: 1 flood/5s, 1 flood/30s, 1 flood/60s, 1 flood/300s
// Topologies: FullMesh 50, Chain 20 (most vulnerable to node silence)

#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

using namespace sim;

struct DutyResult {
    const char* topo;
    int num_nodes;
    float duty_cycle_pct;   // 1/(1+factor)*100
    int   flood_interval_s;
    RoutingStrategy strategy;
    float avg_delivery_rate;
    float avg_latency_ms;
    uint64_t total_airtime_ms;
    uint32_t total_tx;
    uint32_t total_collisions;
};

static DutyResult runDutyCycle(
    RFChannelModel* model,
    const char* topo_name,
    int num_nodes, int grid_rows, int grid_cols,
    float snr,
    float duty_factor,      // getAirtimeBudgetFactor override
    int flood_interval_s,   // seconds between floods
    int num_floods,
    RoutingStrategy strat)
{
    SimBus bus;
    bus.tick_ms = 10;   // coarser tick — these run over long simulated time

    for (int i = 0; i < num_nodes; i++) {
        char name[32];
        if (grid_rows > 0)
            snprintf(name, sizeof(name), "n%d_%d", i/grid_cols, i%grid_cols);
        else
            snprintf(name, sizeof(name), "node%d", i);
        bus.addNode(name, (uint32_t)(i + 1) * 0xdeadbeef);
    }
    bus.channel_model = model;

    for (auto& b : bus.nodes) {
        b.node->routing_strategy    = strat;
        b.node->duty_cycle_factor   = duty_factor;
    }

    // Warmup — shorter, just enough for density priming
    for (int i = 0; i < 2; i++) {
        bus.sendFloodText(0, "warmup");
        uint64_t wp = grid_rows > 0 ? 8000 : 4000;
        bus.run(wp);
    }
    bus.resetStats();

    // Run floods at the specified interval
    uint64_t interval_ms = (uint64_t)flood_interval_s * 1000;
    uint64_t prop_ms = (uint64_t)(flood_interval_s * 1000 > 5000
                       ? 5000 : flood_interval_s * 1000);

    for (int i = 0; i < num_floods; i++) {
        bus.sendFloodText(0, "bench");
        bus.run(prop_ms);
        // Wait out the rest of the interval (budget refills during idle)
        if (interval_ms > prop_ms)
            bus.run(interval_ms - prop_ms);
    }

    auto stats = bus.metrics.aggregate(num_floods);
    uint64_t total_air = 0;
    uint32_t total_tx = 0;
    for (auto& b : bus.nodes) {
        total_air += b.node->total_airtime_ms;
        total_tx  += b.node->total_tx_packets;
    }

    float dc_pct = 1.0f / (1.0f + duty_factor) * 100.f;
    return {
        topo_name, num_nodes, dc_pct, flood_interval_s, strat,
        stats.avg_delivery_rate, stats.avg_latency_ms,
        total_air, total_tx, bus.totalCollisions()
    };
}

static const char* stratName(RoutingStrategy s) {
    return s == RoutingStrategy::DEFAULT ? "DEFAULT " : "ADAPTIVE";
}

int main() {
    printf("MeshCore Duty Cycle Enforcement Scenario\n");
    printf("==========================================\n");
    printf("Budget factors: 50%% (no limit) | 10%% (relaxed) | 1%% (EU legal)\n\n");

    FILE* csv = fopen("dutycycle_results.csv", "w");
    if (csv)
        fprintf(csv, "topo,num_nodes,duty_cycle_pct,flood_interval_s,strategy,"
                     "avg_delivery_rate,avg_latency_ms,total_airtime_ms,total_tx,total_collisions\n");

    struct Config {
        const char* name; int nodes, rows, cols; float snr;
    };
    Config configs[] = {
        { "FM50",  50, 0, 0, 8.0f },
        { "CH20",  20, 0, 0, 8.0f },
    };

    // duty_factor → duty_cycle%:  1→50%  9→10%  99→1%
    struct BudgetLevel { float factor; const char* label; };
    BudgetLevel budgets[] = {
        { 1.0f,  "50% (none)"  },
        { 9.0f,  "10% (relax)" },
        { 99.0f, " 1% (EU)"   },
    };

    int intervals[] = { 5, 30, 60, 300 };  // seconds between floods
    RoutingStrategy strats[] = { RoutingStrategy::DEFAULT, RoutingStrategy::ADAPTIVE };

    for (auto& cfg : configs) {
        RFChannelModel* model = cfg.name[0] == 'C'
            ? (RFChannelModel*)new ChainModel(cfg.snr)
            : (RFChannelModel*)new FullMeshModel(cfg.snr);

        printf("\n=== %s (%d nodes) ===\n", cfg.name, cfg.nodes);

        for (auto& budget : budgets) {
            printf("\n  Duty cycle: %s\n", budget.label);
            printf("  %-8s %-6s %-9s | dr%%     lat(ms)  tx    air(ms)      | Δdr    Δair\n",
                   "interval", "strat", "duty%%");

            for (int ivl : intervals) {
                // 20 floods at short intervals, fewer at long (keep runtime bounded)
                int nf = ivl <= 30 ? 20 : ivl <= 60 ? 15 : 10;

                DutyResult def_r{};
                bool have_def = false;

                for (auto& strat : strats) {
                    auto r = runDutyCycle(model, cfg.name, cfg.nodes,
                                         cfg.rows, cfg.cols, cfg.snr,
                                         budget.factor, ivl, nf, strat);

                    if (!have_def) {
                        def_r = r; have_def = true;
                        printf("  %-8ds %-6s %5.0f%%    | %5.1f%%  %7.0f  %-5u %12lld\n",
                               ivl, stratName(strat), budget.factor == 1.f ? 50.f :
                               budget.factor == 9.f ? 10.f : 1.f,
                               r.avg_delivery_rate*100.f, r.avg_latency_ms,
                               r.total_tx, (long long)r.total_airtime_ms);
                    } else {
                        float dr_d  = (r.avg_delivery_rate - def_r.avg_delivery_rate)*100.f;
                        float air_p = def_r.total_airtime_ms > 0
                            ? (float)((long long)r.total_airtime_ms-(long long)def_r.total_airtime_ms)
                              /(float)def_r.total_airtime_ms*100.f : 0.f;
                        const char* flag = r.avg_delivery_rate < def_r.avg_delivery_rate-0.03f
                                           ? " !!" : "";
                        printf("  %-8ds %-6s %5.0f%%    | %5.1f%%  %7.0f  %-5u %12lld | %+5.1f%% %+5.0f%%%s\n",
                               ivl, stratName(strat), budget.factor == 1.f ? 50.f :
                               budget.factor == 9.f ? 10.f : 1.f,
                               r.avg_delivery_rate*100.f, r.avg_latency_ms,
                               r.total_tx, (long long)r.total_airtime_ms,
                               dr_d, air_p, flag);
                    }

                    if (csv)
                        fprintf(csv, "%s,%d,%.1f,%d,%s,%.4f,%.1f,%lld,%u,%u\n",
                                cfg.name, cfg.nodes, r.duty_cycle_pct, ivl, stratName(strat),
                                r.avg_delivery_rate, r.avg_latency_ms,
                                (long long)r.total_airtime_ms, r.total_tx, r.total_collisions);
                }
            }
        }
        delete model;
    }

    printf("\n=== DUTY CYCLE BUDGET REFERENCE ===\n");
    printf("SF8, BW62.5kHz, 50-byte payload ≈ 623ms airtime per packet\n");
    printf("  1%%  budget/hour: %d ms → ~%d relays/hour/node\n",
           (int)(3600000 / 100), (int)(3600000 / 100 / 623));
    printf("  10%% budget/hour: %d ms → ~%d relays/hour/node\n",
           (int)(3600000 / 10), (int)(3600000 / 10 / 623));
    printf("  50%% budget/hour: %d ms → ~%d relays/hour/node\n",
           (int)(3600000 / 2), (int)(3600000 / 2 / 623));
    printf("\nAt 1%% with ADAPTIVE (10%% of nodes relay): effectively ");
    printf("~%d relays/hour across network per flood\n",
           (int)(3600000 / 100 / 623) * 10);

    if (csv) fclose(csv);
    printf("\nCSV written to dutycycle_results.csv\n");
    return 0;
}
