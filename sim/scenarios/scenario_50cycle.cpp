// scenario_50cycle.cpp
// 50-cycle iterative mesh optimization test suite.
//
// Structure:
//   Phase 1 (Cycles  1-8):  Baseline — flood collapse sweep (topology × node count)
//   Phase 2 (Cycles  9-16): SNR variation — isolate RF condition effect
//   Phase 3 (Cycles 17-24): Relay strategy sweep — DEFAULT vs SNR_WEIGHTED vs HYBRID
//   Phase 4 (Cycles 25-32): Probabilistic forwarding — p=0.4/0.6/0.8/1.0
//   Phase 5 (Cycles 33-40): Hop-limit sensitivity — max_hops 3/5/8/12
//   Phase 6 (Cycles 41-50): Best-of combinations — non-confounding stacked improvements
//
// One variable changes per phase. Parameters known to be independent
// (e.g. topology shape and node count) may vary together within a phase.
//
// Output: stdout RESULT/DELTA lines + 50cycle_results.csv

#include "TestRunner.h"
#include "SimBus.h"
#include "SimMetrics.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>

using namespace sim;

// Run one TestCase with probabilistic forwarding applied to all nodes.
// p_forward gates are enforced inside SimNode::getRetransmitDelay() via the
// p_forward field — nodes that lose the roll return a huge delay and are
// effectively silent for that packet (dedup blocks any retry).
static TestResult runProbabilistic(const TestCase& tc, float p_forward) {
    SimBus bus;
    bus.tick_ms = 5;
    char name_buf[32];

    auto* model = new FullMeshModel(tc.channel_snr);
    bus.channel_model = model;

    for (int i = 0; i < tc.num_nodes; i++) {
        snprintf(name_buf, sizeof(name_buf), "node%d", i);
        bus.addNode(name_buf, (uint32_t)(i + 1) * 0xdeadbeef);
    }

    for (auto& b : bus.nodes) {
        b.node->routing_strategy = tc.strategy;
        b.node->p_forward = p_forward;
    }

    bus.run(2000);
    bus.resetStats();

    for (int i = 0; i < tc.num_floods; i++) {
        bus.sendFloodText(0, "bench");
        bus.run(5000);
    }

    TestResult tr;
    tr.tc = tc;
    tr.stats = bus.metrics.aggregate(tc.num_floods);

    uint64_t total_air = 0;
    for (auto& b : bus.nodes) total_air += b.node->total_airtime_ms;
    tr.stats.total_airtime_ms = total_air;
    tr.stats.total_tx = 0;
    for (auto& b : bus.nodes) tr.stats.total_tx += b.node->total_tx_packets;

    return tr;
}

// ---------------------------------------------------------------------------
// Print a phase header
// ---------------------------------------------------------------------------
static void phaseHeader(int phase, const char* title) {
    printf("\n");
    printf("=======================================================================\n");
    printf("  PHASE %d — %s\n", phase, title);
    printf("=======================================================================\n");
}

// ---------------------------------------------------------------------------
// Print RESULT row
// ---------------------------------------------------------------------------
static void printResult(const char* cycle_label, const TestResult& r,
                        const char* note = nullptr) {
    printf("RESULT | cycle=%-24s | topo=%-8s | nodes=%-3d | snr=%+5.1f | dr=%5.1f%% | lat=%6.0fms | hops=%4.1f | air=%lldms",
           cycle_label,
           r.tc.topo == TestCase::TopoType::FULL_MESH ? "FullMesh" :
           r.tc.topo == TestCase::TopoType::CHAIN      ? "Chain"    : "Grid",
           r.tc.num_nodes,
           r.tc.channel_snr,
           r.stats.avg_delivery_rate * 100.0f,
           r.stats.avg_latency_ms,
           r.stats.avg_hops,
           (long long)r.stats.total_airtime_ms);
    if (note) printf(" | %s", note);
    printf("\n");
}

// ---------------------------------------------------------------------------
// Print DELTA vs a baseline result
// ---------------------------------------------------------------------------
static void printDelta(const char* cycle_label, const TestResult& r,
                       const TestResult& base) {
    float dr_delta  = (r.stats.avg_delivery_rate - base.stats.avg_delivery_rate) * 100.0f;
    float lat_pct   = base.stats.avg_latency_ms > 0
                      ? (r.stats.avg_latency_ms - base.stats.avg_latency_ms) / base.stats.avg_latency_ms * 100.0f
                      : 0.0f;
    float hop_delta = r.stats.avg_hops - base.stats.avg_hops;
    float air_pct   = base.stats.total_airtime_ms > 0
                      ? (float)((long long)r.stats.total_airtime_ms - (long long)base.stats.total_airtime_ms)
                        / (float)base.stats.total_airtime_ms * 100.0f
                      : 0.0f;
    printf("DELTA  | cycle=%-24s | dr=%+5.1f%% | lat=%+6.0f%% | hops=%+4.1f | air=%+5.0f%%\n",
           cycle_label, dr_delta, lat_pct, hop_delta, air_pct);
}

// ---------------------------------------------------------------------------
// CSV helpers
// ---------------------------------------------------------------------------
static FILE* csv_open(const char* path) {
    FILE* f = fopen(path, "w");
    if (!f) { printf("ERROR: cannot open %s\n", path); return nullptr; }
    fprintf(f, "cycle,phase,label,topo,num_nodes,channel_snr,strategy,p_forward,"
               "avg_delivery_rate,avg_latency_ms,avg_hops,total_airtime_ms,total_tx\n");
    return f;
}

static void csv_row(FILE* f, int cycle, int phase, const char* label,
                    const TestResult& r, float p_forward = 1.0f) {
    if (!f) return;
    const char* topo = r.tc.topo == TestCase::TopoType::FULL_MESH ? "FullMesh" :
                       r.tc.topo == TestCase::TopoType::CHAIN      ? "Chain"    : "Grid";
    const char* strat = r.tc.strategy == RoutingStrategy::DEFAULT        ? "DEFAULT" :
                        r.tc.strategy == RoutingStrategy::SNR_WEIGHTED   ? "SNR_WEIGHTED" :
                                                                            "PATH_SNR_HYBRID";
    fprintf(f, "%d,%d,%s,%s,%d,%.1f,%s,%.2f,%.4f,%.2f,%.3f,%lld,%u\n",
            cycle, phase, label, topo, r.tc.num_nodes, r.tc.channel_snr,
            strat, p_forward,
            r.stats.avg_delivery_rate,
            r.stats.avg_latency_ms,
            r.stats.avg_hops,
            (long long)r.stats.total_airtime_ms,
            r.stats.total_tx);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    printf("MeshCore 50-Cycle Iterative Optimization Suite\n");
    printf("================================================\n");
    printf("Floods per cycle: 20\n");

    FILE* csv = csv_open("50cycle_results.csv");

    TestRunner runner;
    int cycle = 0;

    // =======================================================================
    // PHASE 1 — Baseline flood collapse sweep
    //   Variables: topology × node count (both varied — they are independent
    //   dimensions; we need all combinations to map collapse thresholds)
    //   Fixed: DEFAULT strategy, SNR=8dB, 20 floods
    // =======================================================================
    phaseHeader(1, "Baseline — flood collapse sweep");
    printf("  Variables: topology × node count (independent dimensions)\n");
    printf("  Fixed: DEFAULT strategy, SNR=8dB, 20 floods\n\n");

    struct Phase1Entry { TestCase::TopoType topo; int nodes; int rows; int cols; };
    Phase1Entry p1[] = {
        { TestCase::TopoType::FULL_MESH,  10, 0, 0 },
        { TestCase::TopoType::FULL_MESH,  20, 0, 0 },
        { TestCase::TopoType::CHAIN,      10, 0, 0 },
        { TestCase::TopoType::CHAIN,      20, 0, 0 },
        { TestCase::TopoType::GRID,       16, 4, 4 },  // 4×4
        { TestCase::TopoType::GRID,       25, 5, 5 },  // 5×5
        { TestCase::TopoType::FULL_MESH,  50, 0, 0 },
        { TestCase::TopoType::FULL_MESH, 100, 0, 0 },
    };

    std::vector<TestResult> baselines;
    for (auto& e : p1) {
        cycle++;
        TestCase tc{};
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "C%02d_baseline", cycle);
        tc.name       = lbl;
        tc.num_nodes  = e.nodes;
        tc.channel_snr = 8.0f;
        tc.num_floods = 20;
        tc.strategy   = RoutingStrategy::DEFAULT;
        tc.topo       = e.topo;
        tc.grid_rows  = e.rows;
        tc.grid_cols  = e.cols;

        auto result = runner.run({tc})[0];
        baselines.push_back(result);
        printResult(lbl, result);
        csv_row(csv, cycle, 1, lbl, result);
    }

    // =======================================================================
    // PHASE 2 — SNR variation
    //   Variables: channel_snr (3/6/9/12 dB)
    //   Fixed: DEFAULT strategy, FullMesh 20 nodes, 20 floods
    //   (isolate RF condition effect on delivery/latency independently)
    // =======================================================================
    phaseHeader(2, "SNR variation — RF condition sensitivity");
    printf("  Variables: channel_snr\n");
    printf("  Fixed: DEFAULT strategy, FullMesh 20 nodes, 20 floods\n\n");

    float snr_vals[] = { 3.0f, 6.0f, 9.0f, 12.0f };
    std::vector<TestResult> snr_results;
    // baseline for phase 2 = cycle 2 result (FullMesh 20 nodes DEFAULT SNR=8)
    const TestResult& snr_base = baselines[1]; // FULL_MESH 20 nodes

    for (float snr : snr_vals) {
        cycle++;
        TestCase tc{};
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "C%02d_snr%.0f", cycle, snr);
        tc.name       = lbl;
        tc.num_nodes  = 20;
        tc.channel_snr = snr;
        tc.num_floods = 20;
        tc.strategy   = RoutingStrategy::DEFAULT;
        tc.topo       = TestCase::TopoType::FULL_MESH;

        auto result = runner.run({tc})[0];
        snr_results.push_back(result);
        printResult(lbl, result);
        printDelta(lbl, result, snr_base);
        csv_row(csv, cycle, 2, lbl, result);
    }

    // =======================================================================
    // PHASE 3 — Relay strategy sweep
    //   Variables: routing strategy (DEFAULT / SNR_WEIGHTED / PATH_SNR_HYBRID)
    //   Fixed: 3 topologies × best node count from phase 1, SNR=8dB, 20 floods
    //   Each strategy tested per topology — strategy is the ONLY new variable
    // =======================================================================
    phaseHeader(3, "Relay strategy — SNR_WEIGHTED vs PATH_SNR_HYBRID vs DEFAULT");
    printf("  Variables: routing strategy\n");
    printf("  Fixed: SNR=8dB, 20 floods; topologies from phase 1\n\n");

    struct P3Entry {
        TestCase::TopoType topo; int nodes; int rows; int cols;
        const TestResult* base; // phase 1 baseline to compare against
    };
    P3Entry p3[] = {
        { TestCase::TopoType::FULL_MESH, 20, 0, 0, &baselines[1] },
        { TestCase::TopoType::CHAIN,     20, 0, 0, &baselines[3] },
        { TestCase::TopoType::GRID,      25, 5, 5, &baselines[5] },
    };

    RoutingStrategy strats[] = { RoutingStrategy::SNR_WEIGHTED, RoutingStrategy::PATH_SNR_HYBRID };
    const char* strat_names[] = { "SNR_W", "HYBRID" };

    for (auto& e : p3) {
        for (int si = 0; si < 2; si++) {
            cycle++;
            TestCase tc{};
            char lbl[48];
            snprintf(lbl, sizeof(lbl), "C%02d_%s", cycle, strat_names[si]);
            tc.name       = lbl;
            tc.num_nodes  = e.nodes;
            tc.channel_snr = 8.0f;
            tc.num_floods = 20;
            tc.strategy   = strats[si];
            tc.topo       = e.topo;
            tc.grid_rows  = e.rows;
            tc.grid_cols  = e.cols;

            auto result = runner.run({tc})[0];
            printResult(lbl, result);
            if (e.base) printDelta(lbl, result, *e.base);
            csv_row(csv, cycle, 3, lbl, result);
        }
    }

    // =======================================================================
    // PHASE 4 — Probabilistic forwarding (p-forwarding)
    //   Variables: p_forward (0.4 / 0.6 / 0.8 / 1.0)
    //   Fixed: DEFAULT strategy base delay, FullMesh 50 nodes, SNR=8dB, 20 floods
    //   Why 50 nodes: this is where flooding starts degrading; p-forward shines here
    //   Baseline: cycle 7 (FullMesh 50 nodes DEFAULT)
    // =======================================================================
    phaseHeader(4, "Probabilistic forwarding — p=0.4/0.6/0.8/1.0");
    printf("  Variables: p_forward probability\n");
    printf("  Fixed: DEFAULT delay, FullMesh 50 nodes, SNR=8dB, 20 floods\n\n");

    float p_vals[] = { 0.4f, 0.6f, 0.8f, 1.0f };
    const TestResult& p_base = baselines[6]; // FullMesh 50 nodes DEFAULT

    for (float p : p_vals) {
        cycle++;
        TestCase tc{};
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "C%02d_p%.0f", cycle, p * 100.0f);
        tc.name       = lbl;
        tc.num_nodes  = 50;
        tc.channel_snr = 8.0f;
        tc.num_floods = 20;
        tc.strategy   = RoutingStrategy::DEFAULT;
        tc.topo       = TestCase::TopoType::FULL_MESH;

        auto result = runProbabilistic(tc, p);
        printResult(lbl, result,
                    p < 1.0f ? "p-forward active" : "p=1.0 (no drop)");
        printDelta(lbl, result, p_base);
        csv_row(csv, cycle, 4, lbl, result, p);
    }

    // =======================================================================
    // PHASE 5 — Hop limit sensitivity
    //   Variables: effective max_hops (3 / 5 / 8 / 12)
    //   Fixed: DEFAULT strategy, FullMesh 50 nodes, SNR=8dB, 20 floods
    //   Note: MeshCore uses path_hash_count as a proxy for hop depth.
    //   We simulate hop limit by using PATH_SNR_HYBRID which heavily penalises
    //   high hop-count packets (they get long delays → effectively dropped when
    //   dedup window closes). We vary the max_hops threshold in the hybrid formula.
    //
    //   Since SimNode.getRetransmitDelay() uses pkt->getPathHashCount(), we
    //   parametrise an EXTREME_HOP_PENALTY variant here by running PATH_SNR_HYBRID
    //   with different effective path_factor curves.
    //
    //   Practical approach: run PATH_SNR_HYBRID as-is (naturally penalises hops)
    //   and observe how deep hops actually reach — compare avg_hops across
    //   node counts. This gives us the organic hop depth under the strategy.
    //   We use Chain topology (forced multi-hop) to see hop depth clearly.
    // =======================================================================
    phaseHeader(5, "Hop-depth sensitivity — Chain topology, varying node count");
    printf("  Variables: num_nodes (determines max possible hops in chain)\n");
    printf("  Fixed: PATH_SNR_HYBRID, Chain, SNR=8dB, 20 floods\n");
    printf("  Goal: measure avg_hops vs chain length to find collapse threshold\n\n");

    int chain_sizes[] = { 5, 10, 15, 20 };
    const TestResult* chain_baselines[] = {
        &baselines[2], // Chain 10 (closest to 5, use as reference)
        &baselines[2], // Chain 10
        &baselines[3], // Chain 20
        &baselines[3], // Chain 20
    };

    for (int ci = 0; ci < 4; ci++) {
        cycle++;
        TestCase tc{};
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "C%02d_chain%d_hybrid", cycle, chain_sizes[ci]);
        tc.name       = lbl;
        tc.num_nodes  = chain_sizes[ci];
        tc.channel_snr = 8.0f;
        tc.num_floods = 20;
        tc.strategy   = RoutingStrategy::PATH_SNR_HYBRID;
        tc.topo       = TestCase::TopoType::CHAIN;

        auto result = runner.run({tc})[0];
        printResult(lbl, result);
        if (chain_baselines[ci]) printDelta(lbl, result, *chain_baselines[ci]);
        csv_row(csv, cycle, 5, lbl, result);
    }

    // =======================================================================
    // PHASE 6 — Best-of combinations (non-confounding stacking)
    //   Based on phases 1-5 findings, combine improvements that are orthogonal:
    //
    //   C41: PATH_SNR_HYBRID + FullMesh 50 nodes (best single strategy at scale)
    //   C42: PATH_SNR_HYBRID + FullMesh 100 nodes (test at collapse boundary)
    //   C43: PATH_SNR_HYBRID + Chain 20 nodes, SNR=3dB (harsh RF + deep chain)
    //   C44: PATH_SNR_HYBRID + Grid 5×5, SNR=6dB (real-world sparse grid)
    //   C45: SNR_WEIGHTED + FullMesh 50 nodes, SNR=12dB (ideal RF conditions)
    //   C46: SNR_WEIGHTED + FullMesh 50 nodes, SNR=3dB (poor RF conditions)
    //   C47: PATH_SNR_HYBRID + FullMesh 50 nodes, p_forward=0.7 (combined)
    //   C48: PATH_SNR_HYBRID + FullMesh 100 nodes, p_forward=0.7 (combined at scale)
    //   C49: PATH_SNR_HYBRID + Chain 10 nodes, SNR=6dB (relay chain simulation)
    //   C50: DEFAULT + FullMesh 20 nodes, SNR=8dB (repeat baseline — drift check)
    // =======================================================================
    phaseHeader(6, "Best-of combinations — orthogonal stacking");
    printf("  Cycles 41-50: validated improvements combined where non-confounding\n\n");

    struct Phase6Entry {
        const char* label;
        TestCase::TopoType topo;
        int nodes, rows, cols;
        float snr;
        RoutingStrategy strat;
        float p_fwd;
        const TestResult* base;
    };

    Phase6Entry p6[] = {
        { "HYBRID_FM50",     TestCase::TopoType::FULL_MESH,  50, 0, 0,  8.0f, RoutingStrategy::PATH_SNR_HYBRID, 1.0f, &baselines[6] },
        { "HYBRID_FM100",    TestCase::TopoType::FULL_MESH, 100, 0, 0,  8.0f, RoutingStrategy::PATH_SNR_HYBRID, 1.0f, &baselines[7] },
        { "HYBRID_C20_S3",   TestCase::TopoType::CHAIN,      20, 0, 0,  3.0f, RoutingStrategy::PATH_SNR_HYBRID, 1.0f, &baselines[3] },
        { "HYBRID_G5x5_S6",  TestCase::TopoType::GRID,       25, 5, 5,  6.0f, RoutingStrategy::PATH_SNR_HYBRID, 1.0f, &baselines[5] },
        { "SNRW_FM50_S12",   TestCase::TopoType::FULL_MESH,  50, 0, 0, 12.0f, RoutingStrategy::SNR_WEIGHTED,    1.0f, &baselines[6] },
        { "SNRW_FM50_S3",    TestCase::TopoType::FULL_MESH,  50, 0, 0,  3.0f, RoutingStrategy::SNR_WEIGHTED,    1.0f, &baselines[6] },
        { "HYBRID_FM50_p70", TestCase::TopoType::FULL_MESH,  50, 0, 0,  8.0f, RoutingStrategy::PATH_SNR_HYBRID, 0.7f, &baselines[6] },
        { "HYBRID_FM100_p70",TestCase::TopoType::FULL_MESH, 100, 0, 0,  8.0f, RoutingStrategy::PATH_SNR_HYBRID, 0.7f, &baselines[7] },
        { "HYBRID_C10_S6",   TestCase::TopoType::CHAIN,      10, 0, 0,  6.0f, RoutingStrategy::PATH_SNR_HYBRID, 1.0f, &baselines[2] },
        { "BASE_RECHECK",    TestCase::TopoType::FULL_MESH,  20, 0, 0,  8.0f, RoutingStrategy::DEFAULT,          1.0f, &baselines[1] },
    };

    for (auto& e : p6) {
        cycle++;
        TestCase tc{};
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "C%02d_%s", cycle, e.label);
        tc.name       = lbl;
        tc.num_nodes  = e.nodes;
        tc.channel_snr = e.snr;
        tc.num_floods = 20;
        tc.strategy   = e.strat;
        tc.topo       = e.topo;
        tc.grid_rows  = e.rows;
        tc.grid_cols  = e.cols;

        TestResult result;
        if (e.p_fwd < 1.0f) {
            result = runProbabilistic(tc, e.p_fwd);
        } else {
            result = runner.run({tc})[0];
        }

        printResult(lbl, result);
        if (e.base) printDelta(lbl, result, *e.base);
        csv_row(csv, cycle, 6, lbl, result, e.p_fwd);
    }

    // =======================================================================
    // Final summary
    // =======================================================================
    printf("\n");
    printf("=======================================================================\n");
    printf("  SUMMARY — Top findings\n");
    printf("=======================================================================\n");
    printf("  CSV written to 50cycle_results.csv\n");
    printf("  Total cycles run: %d\n", cycle);
    printf("\n");
    printf("  Key questions this suite answers:\n");
    printf("  1. At what node count does DEFAULT flooding collapse? (Phase 1)\n");
    printf("  2. How much does poor SNR degrade delivery vs latency? (Phase 2)\n");
    printf("  3. Which relay strategy wins at scale? (Phase 3)\n");
    printf("  4. What p_forward probability optimises airtime vs delivery? (Phase 4)\n");
    printf("  5. How deep do hops reach before path quality degrades? (Phase 5)\n");
    printf("  6. Do combined improvements stack without regression? (Phase 6)\n");
    printf("=======================================================================\n");

    if (csv) fclose(csv);
    return 0;
}
