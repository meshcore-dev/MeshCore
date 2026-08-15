// scenario_txpower.cpp
// Tests adaptive TX power reduction in dense areas.
//
// Hypothesis: In a dense full-mesh network, reducing TX power when in DENSE/MEDIUM
// tier provides:
//   1. Battery savings — lower TX current × same airtime = less energy
//   2. Reduced interference radius — quieter nodes don't stomp on distant clusters
//   3. Capture effect benefit — nearby nodes still win capture battles
//
// Counter-hypothesis: In a chain topology, TX power reduction kills hop range
// on marginal links and collapses delivery.
//
// Test matrix:
//   Power save levels: OFF, CONSERVATIVE (-6dB DENSE), AGGRESSIVE (-10dB DENSE/-6dB MEDIUM)
//   Topologies: FullMesh 50, FullMesh 100, Chain 20, Grid 5×5
//   Strategy: ADAPTIVE only (power-save only makes sense paired with hash-gate suppression)
//
// Energy model: SX1262 typical current draw
//   +20 dBm → 120 mA TX   5.5 mA RX
//   +14 dBm →  45 mA TX
//   +10 dBm →  25 mA TX
//
// "Festival weekend" use case: 48-hour runtime estimate at each power level.

#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

using namespace sim;

struct PowerResult {
    const char* topo;
    int num_nodes;
    const char* power_mode;
    float dense_dbm;
    float medium_dbm;
    float avg_delivery_rate;
    float avg_latency_ms;
    uint32_t total_tx;
    uint32_t total_collisions;
    float total_tx_energy_mah;   // TX energy across all nodes
    float total_rx_energy_mah;   // RX energy across all nodes
    float total_energy_mah;
    float per_node_energy_mah;   // average per node
};

struct PowerLevel {
    const char* name;
    float dense_dbm;
    float medium_dbm;
    bool  enabled;
};

static PowerResult runPowerTest(
    RFChannelModel* model,
    const char* topo_name,
    int num_nodes, int grid_rows, int grid_cols,
    float snr,
    const PowerLevel& plevel,
    int num_floods)
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

    for (auto& b : bus.nodes) {
        b.node->routing_strategy      = RoutingStrategy::ADAPTIVE;
        b.node->power_save_enabled    = plevel.enabled;
        b.node->power_save_dense_dbm  = plevel.dense_dbm;
        b.node->power_save_medium_dbm = plevel.medium_dbm;
        b.node->full_power_dbm        = 20.0f;
        b.radio->tx_power_dbm         = 20.0f;
    }

    // Warmup — prime density estimators.
    // Need enough floods + time for relay storms to settle so density observations
    // accumulate. FM100 needs more time — initial collision storm destroys most
    // relay packets; later floods get through as relay timing spreads out.
    uint64_t warmup_ms = grid_rows > 0 ? 8000 : 3000;
    for (int i = 0; i < 3; i++) {
        bus.sendFloodText(0, "warmup");
        bus.run(warmup_ms);
    }
    bus.resetStats();

    uint64_t prop_ms = grid_rows > 0 ? 8000 : 5000;
    for (int i = 0; i < num_floods; i++) {
        bus.sendFloodText(0, "bench");
        bus.run(prop_ms);
    }

    auto stats = bus.metrics.aggregate(num_floods);

    float tx_energy = 0, rx_energy = 0;
    uint32_t total_tx = 0;
    for (auto& b : bus.nodes) {
        tx_energy += b.node->total_tx_energy_mah;
        rx_energy += b.node->total_rx_energy_mah;
        total_tx  += b.node->total_tx_packets;
    }
    float total_energy = tx_energy + rx_energy;

    return {
        topo_name, num_nodes, plevel.name, plevel.dense_dbm, plevel.medium_dbm,
        stats.avg_delivery_rate, stats.avg_latency_ms,
        total_tx, bus.totalCollisions(),
        tx_energy, rx_energy, total_energy,
        num_nodes > 0 ? total_energy / (float)num_nodes : 0.0f
    };
}

static const char* fmtDbm(float dbm) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%+.0fdBm", dbm - 20.0f);
    return buf;
}

int main() {
    printf("MeshCore Adaptive TX Power Scenario\n");
    printf("=====================================\n");
    printf("ADAPTIVE strategy + variable TX power reduction in DENSE/MEDIUM tier\n");
    printf("Battery model: SX1262 typical current (120/45/25 mA at 20/14/10 dBm)\n\n");

    FILE* csv = fopen("txpower_results.csv", "w");
    if (csv)
        fprintf(csv, "topo,num_nodes,power_mode,dense_dbm,medium_dbm,"
                     "avg_delivery_rate,avg_latency_ms,total_tx,total_collisions,"
                     "total_tx_energy_mah,total_rx_energy_mah,total_energy_mah,per_node_energy_mah\n");

    // DC6x6: dense 6×6 positional cluster — each node hears ~8 neighbors.
    //   Range=1.5, spacing=1.0 → most nodes see full 3×3 neighborhood.
    //   This models a festival grounds: dense, positional, realistic.
    // FM50: FullMesh 50, the validated ADAPTIVE scenario.
    // CH20: chain — verifies power-save doesn't hurt sparse hop range.
    struct Config {
        const char* name; int nodes, rows, cols; float snr; bool dense_cluster;
    };
    Config configs[] = {
        { "FM50",   50,  0, 0, 8.0f, false },
        { "DC6x6",  36,  6, 6, 8.0f, true  },  // dense 6×6 positional cluster
        { "CH20",   20,  0, 0, 8.0f, false },
    };

    // Power save levels to test.
    // Dense tier gets more aggressive reduction; medium tier gets conservative.
    PowerLevel levels[] = {
        { "FULL_PWR",    20.0f, 20.0f, false },  // baseline: no power save
        { "CONSERV",     14.0f, 17.0f, true  },  // conservative: -6dB DENSE, -3dB MEDIUM
        { "MODERATE",    10.0f, 14.0f, true  },  // moderate: -10dB DENSE, -6dB MEDIUM
        { "AGGRESSIVE",   7.0f, 10.0f, true  },  // aggressive: -13dB DENSE, -10dB MEDIUM
    };

    for (auto& cfg : configs) {
        RFChannelModel* model = nullptr;
        if (cfg.dense_cluster) {
            // Dense positional cluster: range=1.5, spacing=1.0
            // Interior nodes each hear ~8 neighbors → DENSE tier (≥15 after relays)
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

        printf("\n=== %s (%d nodes, SNR=%.0fdB) ===\n", cfg.name, cfg.nodes, cfg.snr);
        printf("  %-12s %-14s | dr%%     lat(ms)  tx    coll   "
               "tx_E(mAh)  rx_E(mAh) tot_E(mAh) node_E(mAh) | Δdr    Δenergy\n",
               "mode", "dense/med pwr");

        PowerResult baseline{};
        bool have_base = false;

        int num_floods = 20;

        for (auto& lvl : levels) {
            auto r = runPowerTest(model, cfg.name, cfg.nodes,
                                  cfg.rows, cfg.cols, cfg.snr, lvl, num_floods);

            char pwr_label[32];
            snprintf(pwr_label, sizeof(pwr_label), "%+.0f/%+.0fdBm",
                     lvl.dense_dbm - 20.0f, lvl.medium_dbm - 20.0f);

            if (!have_base) {
                baseline = r; have_base = true;
                printf("  %-12s %-14s | %5.1f%%  %7.0f  %-5u %-6u "
                       "%9.3f  %9.3f  %9.3f  %9.3f\n",
                       lvl.name, pwr_label,
                       r.avg_delivery_rate*100.f, r.avg_latency_ms,
                       r.total_tx, r.total_collisions,
                       r.total_tx_energy_mah, r.total_rx_energy_mah,
                       r.total_energy_mah, r.per_node_energy_mah);
            } else {
                float dr_d   = (r.avg_delivery_rate - baseline.avg_delivery_rate)*100.f;
                float e_pct  = baseline.total_energy_mah > 0
                    ? (r.total_energy_mah - baseline.total_energy_mah)
                      / baseline.total_energy_mah * 100.f : 0.f;
                const char* flag = r.avg_delivery_rate < baseline.avg_delivery_rate - 0.03f
                                   ? " !!" : "";
                printf("  %-12s %-14s | %5.1f%%  %7.0f  %-5u %-6u "
                       "%9.3f  %9.3f  %9.3f  %9.3f | %+5.1f%% %+6.1f%%%s\n",
                       lvl.name, pwr_label,
                       r.avg_delivery_rate*100.f, r.avg_latency_ms,
                       r.total_tx, r.total_collisions,
                       r.total_tx_energy_mah, r.total_rx_energy_mah,
                       r.total_energy_mah, r.per_node_energy_mah,
                       dr_d, e_pct, flag);
            }

            if (csv)
                fprintf(csv, "%s,%d,%s,%.1f,%.1f,%.4f,%.1f,%u,%u,%.4f,%.4f,%.4f,%.4f\n",
                        cfg.name, cfg.nodes, lvl.name, lvl.dense_dbm, lvl.medium_dbm,
                        r.avg_delivery_rate, r.avg_latency_ms,
                        r.total_tx, r.total_collisions,
                        r.total_tx_energy_mah, r.total_rx_energy_mah,
                        r.total_energy_mah, r.per_node_energy_mah);
        }

        delete model;
    }

    // Festival weekend projection: 48-hour runtime
    printf("\n=== FESTIVAL WEEKEND PROJECTION (48-hour runtime) ===\n");
    printf("Assumptions: FM50, 1 flood/30s, SF8, SX1262 radio\n");
    printf("Battery: 2000 mAh (typical USB power bank to LoRa node)\n");
    printf("Floods/hour: 120  |  TX per flood (ADAPTIVE DENSE): ~7 nodes\n\n");

    // Per-node per-flood energy at each power level (TX component dominates)
    struct FestEst {
        const char* mode;
        float tx_dbm;
        float relay_fraction;  // fraction of floods this node relays (DENSE hash gate)
    };
    FestEst fests[] = {
        { "FULL_PWR  ", 20.0f, 0.15f },
        { "CONSERV   ", 14.0f, 0.15f },
        { "MODERATE  ", 10.0f, 0.15f },
        { "AGGRESSIVE",  7.0f, 0.15f },
    };

    float airtime_s = 0.623f;  // ~623ms per packet at SF8 BW62.5
    float floods_per_hour = 120.0f;
    float rx_fraction = 1.0f;  // node receives every flood (full mesh)
    float battery_mah = 2000.0f;

    printf("  %-12s  TX pwr   TX mAh/hr  RX mAh/hr  Tot mAh/hr  Hours\n", "Mode");
    for (auto& f : fests) {
        float tx_ma   = SimNode::txCurrentMa(f.tx_dbm);
        float tx_mah_hr = tx_ma * floods_per_hour * f.relay_fraction * airtime_s / 3600.f;
        float rx_mah_hr = SimNode::RX_CURRENT_MA * floods_per_hour * rx_fraction * airtime_s / 3600.f;
        float tot_mah_hr = tx_mah_hr + rx_mah_hr;
        float hours = battery_mah / tot_mah_hr;
        printf("  %-12s  %+.0fdBm   %7.3f    %7.3f    %8.3f   %5.0f hrs\n",
               f.mode, f.tx_dbm - 20.0f, tx_mah_hr, rx_mah_hr, tot_mah_hr, hours);
    }

    printf("\nNote: excludes MCU idle current (~10-30 mA) which dominates in practice.\n");
    printf("With MCU at 20mA continuous: add ~9.6 mAh/hr → max ~130hrs on 2000mAh.\n");
    printf("TX power reduction still meaningful: saves TX peak current spikes and heat.\n");

    if (csv) fclose(csv);
    printf("\nCSV written to txpower_results.csv\n");
    return 0;
}
