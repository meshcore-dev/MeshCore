// scenario_relay_pct_sweep.cpp
// Sweeps DENSE and MEDIUM relay percentages to find the optimal values for
// hash-based relay selection in the ADAPTIVE strategy.
//
// SPARSE is fixed at 100% — in sparse topologies every relay counts.
// MEDIUM and DENSE are swept independently across a grid of values.
//
// For each (dense_pct, medium_pct) pair we run three topologies:
//   - FullMesh 50 nodes  (primary dense stress test)
//   - FullMesh 100 nodes (extreme density)
//   - Chain 20 nodes     (sparse — must not degrade)
//   - Grid 5×5 25 nodes  (medium density baseline)
//
// Scoring metric: weighted composite score that rewards:
//   delivery_rate (primary) × airtime_efficiency (secondary)
//   We want high DR with low airtime — the Pareto frontier.
//
// Output: sweep_results.csv + ranked table by composite score.

#include "TestRunner.h"
#include "RoutingStrategies.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>

using namespace sim;

// Override the relay percentages at runtime by patching adaptiveRelayPct.
// We do this by storing the current sweep values in a global that the
// strategy function reads. This avoids recompiling per sweep point.
int g_dense_pct  = 12;
int g_medium_pct = 35;

// Override adaptiveRelayPct to use the sweep globals.
// We shadow the inline in RoutingStrategies.h by redefining it here.
// Since RoutingStrategies.h uses an inline, we need to call our own version
// from SimNode. We'll use a different approach: wrap the strategy dispatch
// in a thin shim by overriding via a global function pointer.
//
// Cleaner approach: SimNode already calls adaptiveRelayPct() which is an
// inline. We can't easily override it at runtime without modifying SimNode.
// Instead: define RELAY_PCT_OVERRIDE before including SimNode.h in this
// compilation unit, and provide our own version of the function.
//
// Cleanest approach for this sweep: run each config via a custom runOne()
// that sets p_forward per-node based on density tier, bypassing the inline.
// Since the hash gate reads adaptiveRelayPct() from RoutingStrategies.h,
// we patch it by #defining the values before the include.
//
// Given the build structure, the simplest correct approach is:
// Use a subclassed SimNode that overrides getRetransmitDelay() to read
// g_dense_pct and g_medium_pct. We build that inline here.

#include "SimBus.h"

// ---------------------------------------------------------------------------
// SweepNode — SimNode variant that reads relay pcts from globals.
// ---------------------------------------------------------------------------
namespace sim {

class SweepNode : public SimNode {
public:
    SweepNode(int idx, const std::string& name,
              SimRadio& radio, SimMillisClock& ms, SimRNG& rng,
              SimRTCClock& rtc, SimPacketManager& mgr, SimTables& tables)
      : SimNode(idx, name, radio, ms, rng, rtc, mgr, tables)
    {}

protected:
    uint32_t getRetransmitDelay(const mesh::Packet* pkt) override {
        uint64_t now_ms = (uint64_t)_ms_ref.getMillis();

        if (routing_strategy == RoutingStrategy::ADAPTIVE) {
            DensityTier tier = density.tier(now_ms);

            int relay_pct;
            switch (tier) {
                case DensityTier::SPARSE: relay_pct = 100;           break;
                case DensityTier::MEDIUM: relay_pct = g_medium_pct;  break;
                case DensityTier::DENSE:  relay_pct = g_dense_pct;   break;
                default:                  relay_pct = 100;            break;
            }

            if (relay_pct < 100) {
                uint32_t pkt_seed = 0;
                uint32_t node_seed = (uint32_t)node_idx * 0x9e3779b9u;
                if (pkt->getRawLength() >= 4) memcpy(&pkt_seed, pkt->payload, 4);
                if (!hashBasedRelay(pkt_seed, node_seed, relay_pct)) {
                    return 999999u;
                }
            }
        } else if (p_forward < 1.0f) {
            uint32_t roll = _rng_ref.nextInt(0, 10000);
            if ((float)roll / 10000.0f >= p_forward) return 999999u;
        }

        uint32_t base = (_radio_ref.getEstAirtimeFor(pkt->getRawLength()) * 52 / 50) / 2;
        if (base == 0) base = 10;

        float snr    = _radio_ref.getLastSNR();
        uint8_t hops = pkt->getPathHashCount();
        uint32_t rv  = _rng_ref.nextInt(0, 10000);

        switch (routing_strategy) {
            case RoutingStrategy::SNR_WEIGHTED:   return snrWeightedDelay(base, snr, rv);
            case RoutingStrategy::PATH_SNR_HYBRID: return pathSnrHybridDelay(base, snr, hops, rv);
            case RoutingStrategy::ADAPTIVE:        return adaptiveDelay(base, density.tier(now_ms), snr, hops, rv);
            default:                               return _rng_ref.nextInt(0, 5) * base;
        }
    }
};

}  // namespace sim

// ---------------------------------------------------------------------------
// SweepBus — SimBus that creates SweepNode instead of SimNode.
// ---------------------------------------------------------------------------
class SweepBus : public sim::SimBus {
public:
    int addSweepNode(const std::string& name, uint32_t rng_seed = 0) {
        int idx = (int)nodes.size();
        auto& b = nodes.emplace_back();

        b.ms    = std::make_unique<sim::SimMillisClock>(clock);
        b.rtc   = std::make_unique<sim::SimRTCClock>(clock);
        b.rng   = std::make_unique<sim::SimRNG>(rng_seed ? rng_seed : (uint32_t)(idx * 0x9e3779b9 + 1));
        b.mgr   = std::make_unique<sim::SimPacketManager>(clock);
        b.tables= std::make_unique<sim::SimTables>();

        auto cb = [this, idx](int src, const uint8_t* bytes, int len, uint32_t airtime_ms, float tx_power_dbm) {
            this->onTransmit(src, bytes, len, airtime_ms, tx_power_dbm);
        };
        b.radio = std::make_unique<sim::SimRadio>(idx, cb);

        // Create SweepNode instead of SimNode
        auto* sn = new sim::SweepNode(idx, name, *b.radio, *b.ms, *b.rng, *b.rtc, *b.mgr, *b.tables);
        b.node.reset(sn);
        b.node->init();

        int idx_capture = idx;
        b.node->on_recv = [this, idx_capture](sim::SimNode* n, const sim::DeliveryEvent& ev) {
            uint64_t compound = ((uint64_t)n->last_advert_src_key << 32) | n->last_advert_ts;
            auto it = _tsnode_to_seq.find(compound);
            int seq = it != _tsnode_to_seq.end() ? it->second : -1;
            if (seq < 0) return;
            uint64_t inj = _inject_times.count(seq) ? _inject_times[seq] : 0;
            auto src_it = _seq_to_src.find(seq);
            int src = src_it != _seq_to_src.end() ? src_it->second : -1;
            metrics.record(src, idx_capture, seq, inj,
                           (uint64_t)n->_ms_ref.getMillis(),
                           ev.hop_count, ev.route_type, ev.snr, ev.airtime_ms);
        };

        metrics.setNumNodes((int)nodes.size());
        return idx;
    }

};

// ---------------------------------------------------------------------------
// Run one sweep point — returns (delivery_rate, airtime_ms) per topology
// ---------------------------------------------------------------------------
struct TopoResult {
    float    dr;
    float    lat_ms;
    uint64_t air_ms;
    uint32_t total_tx;
};

static TopoResult runSweepTopo(
    sim::TestCase::TopoType topo, int num_nodes, int rows, int cols,
    float snr, int num_floods)
{
    SweepBus bus;
    bus.tick_ms = 5;
    char name_buf[32];

    sim::RFChannelModel* model = nullptr;
    if (topo == sim::TestCase::TopoType::FULL_MESH) {
        model = new sim::FullMeshModel(snr);
    } else if (topo == sim::TestCase::TopoType::CHAIN) {
        model = new sim::ChainModel(snr);
    } else {
        auto* pm = new sim::PositionalModel(1.5f, snr + 4.0f, snr);
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++)
                pm->addNode((float)c, (float)r);
        model = pm;
    }
    bus.channel_model = model;

    for (int i = 0; i < num_nodes; i++) {
        if (topo == sim::TestCase::TopoType::GRID)
            snprintf(name_buf, sizeof(name_buf), "n%d_%d", i/cols, i%cols);
        else
            snprintf(name_buf, sizeof(name_buf), "node%d", i);
        bus.addSweepNode(name_buf, (uint32_t)(i + 1) * 0xdeadbeef);
    }

    for (auto& b : bus.nodes)
        b.node->routing_strategy = sim::RoutingStrategy::ADAPTIVE;

    // Warmup floods to prime density estimator
    for (int i = 0; i < 3; i++) {
        bus.sendFloodText(0, "warmup");
        uint64_t wp = (topo == sim::TestCase::TopoType::CHAIN) ? 6000 :
                      (topo == sim::TestCase::TopoType::GRID)  ? 8000 : 3000;
        bus.run(wp);
    }
    bus.resetStats();

    uint64_t prop_ms = (topo == sim::TestCase::TopoType::CHAIN) ? 6000 :
                       (topo == sim::TestCase::TopoType::GRID)  ? 8000 : 3000;

    for (int i = 0; i < num_floods; i++) {
        bus.sendFloodText(0, "bench");
        bus.run(prop_ms);
    }

    auto stats = bus.metrics.aggregate(num_floods);
    uint64_t total_air = 0;
    uint32_t total_tx = 0;
    for (auto& b : bus.nodes) {
        total_air += b.node->total_airtime_ms;
        total_tx  += b.node->total_tx_packets;
    }

    delete model;
    return { stats.avg_delivery_rate, stats.avg_latency_ms, total_air, total_tx };
}

// ---------------------------------------------------------------------------
// Scoring: composite score for ranking sweep results.
//
// We want to maximise delivery and minimise airtime waste.
// Primary: delivery rate (must not drop below threshold vs DEFAULT baseline).
// Secondary: airtime efficiency relative to DEFAULT.
//
// Score = delivery_rate^2 / (airtime_ratio)
//   where airtime_ratio = sweep_air / default_air (1.0 = same as DEFAULT)
//
// This penalises both low delivery AND high airtime waste.
// Bonus multiplier if delivery_rate >= 0.99 (no meaningful loss vs DEFAULT).
// ---------------------------------------------------------------------------
struct SweepResult {
    int dense_pct;
    int medium_pct;
    // Per topology
    TopoResult fm50, fm100, chain20, grid5x5;
    // Composite
    float score;
};

static float computeScore(const SweepResult& r,
                          const SweepResult& def) {
    // Weighted average across topologies
    // FullMesh topologies are where density matters most → higher weight
    auto topo_score = [](const TopoResult& s, const TopoResult& d) -> float {
        if (d.air_ms == 0) return 0.0f;
        float air_ratio = (float)s.air_ms / (float)d.air_ms;
        float dr = s.dr;
        // Penalise hard any delivery rate below 95%
        if (dr < 0.95f) dr *= 0.5f;
        return (dr * dr) / air_ratio;
    };

    float s = topo_score(r.fm50,    def.fm50)    * 3.0f   // primary stress test
            + topo_score(r.fm100,   def.fm100)   * 3.0f   // extreme density
            + topo_score(r.chain20, def.chain20) * 2.0f   // sparse must not regress
            + topo_score(r.grid5x5, def.grid5x5) * 2.0f; // medium density
    return s / 10.0f;
}

int main() {
    printf("MeshCore Relay Percentage Sweep\n");
    printf("=================================\n");
    printf("Sweeping DENSE%% × MEDIUM%% for ADAPTIVE hash-based relay selection.\n");
    printf("SPARSE always = 100%% (every relay matters in sparse topology).\n\n");

    const int NUM_FLOODS = 20;

    // Sweep values — cover the interesting range with enough resolution
    // to find the Pareto frontier without taking all day
    int dense_vals[]  = { 5, 8, 10, 12, 15, 20, 25, 30 };
    int medium_vals[] = { 20, 25, 30, 35, 40, 50, 60, 75, 100 };

    int nd = sizeof(dense_vals)  / sizeof(dense_vals[0]);
    int nm = sizeof(medium_vals) / sizeof(medium_vals[0]);

    // Skip invalid combos where medium < dense (medium must >= dense to be consistent)
    // Also skip dense >= 50 (not really "dense suppression" anymore)

    FILE* csv = fopen("sweep_results.csv", "w");
    if (csv) {
        fprintf(csv, "dense_pct,medium_pct,"
                     "fm50_dr,fm50_lat,fm50_air,fm50_tx,"
                     "fm100_dr,fm100_lat,fm100_air,fm100_tx,"
                     "chain20_dr,chain20_lat,chain20_air,chain20_tx,"
                     "grid5x5_dr,grid5x5_lat,grid5x5_air,grid5x5_tx,"
                     "score\n");
    }

    // First, establish DEFAULT baselines for scoring
    printf("Running DEFAULT baselines...\n");
    g_dense_pct = 100; g_medium_pct = 100;  // ADAPTIVE at 100% = effectively DEFAULT timing
    SweepResult def_result{};
    def_result.dense_pct = 100; def_result.medium_pct = 100;
    def_result.fm50    = runSweepTopo(sim::TestCase::TopoType::FULL_MESH, 50, 0, 0, 8.0f, NUM_FLOODS);
    def_result.fm100   = runSweepTopo(sim::TestCase::TopoType::FULL_MESH,100, 0, 0, 8.0f, NUM_FLOODS);
    def_result.chain20 = runSweepTopo(sim::TestCase::TopoType::CHAIN,    20, 0, 0, 8.0f, NUM_FLOODS);
    def_result.grid5x5 = runSweepTopo(sim::TestCase::TopoType::GRID,     25, 5, 5, 8.0f, NUM_FLOODS);
    def_result.score = 1.0f;

    printf("  DEFAULT baselines: FM50 dr=%.1f%% air=%llums | FM100 dr=%.1f%% air=%llums | "
           "Chain20 dr=%.1f%% air=%llums | Grid5x5 dr=%.1f%% air=%llums\n\n",
           def_result.fm50.dr*100, (long long)def_result.fm50.air_ms,
           def_result.fm100.dr*100, (long long)def_result.fm100.air_ms,
           def_result.chain20.dr*100, (long long)def_result.chain20.air_ms,
           def_result.grid5x5.dr*100, (long long)def_result.grid5x5.air_ms);

    int total_points = 0;
    for (int di = 0; di < nd; di++)
        for (int mi = 0; mi < nm; mi++)
            if (medium_vals[mi] >= dense_vals[di]) total_points++;

    printf("Sweeping %d combinations (dense × medium grid)...\n\n", total_points);
    printf("%-6s %-8s | %-20s | %-20s | %-20s | %-20s | SCORE\n",
           "DENSE", "MEDIUM",
           "FM50 (dr%/air/tx)", "FM100 (dr%/air/tx)",
           "Chain20 (dr%/air/tx)", "Grid5x5 (dr%/air/tx)");
    printf("%s\n", std::string(130, '-').c_str());

    std::vector<SweepResult> results;
    int done = 0;

    for (int di = 0; di < nd; di++) {
        for (int mi = 0; mi < nm; mi++) {
            int dp = dense_vals[di];
            int mp = medium_vals[mi];
            if (mp < dp) continue;  // medium must be >= dense

            g_dense_pct  = dp;
            g_medium_pct = mp;

            SweepResult sr{};
            sr.dense_pct  = dp;
            sr.medium_pct = mp;
            sr.fm50    = runSweepTopo(sim::TestCase::TopoType::FULL_MESH, 50, 0, 0, 8.0f, NUM_FLOODS);
            sr.fm100   = runSweepTopo(sim::TestCase::TopoType::FULL_MESH,100, 0, 0, 8.0f, NUM_FLOODS);
            sr.chain20 = runSweepTopo(sim::TestCase::TopoType::CHAIN,    20, 0, 0, 8.0f, NUM_FLOODS);
            sr.grid5x5 = runSweepTopo(sim::TestCase::TopoType::GRID,     25, 5, 5, 8.0f, NUM_FLOODS);
            sr.score = computeScore(sr, def_result);

            done++;
            printf("%-6d %-8d | %5.1f%% %7llums %3u | %5.1f%% %7llums %3u | %5.1f%% %7llums %3u | %5.1f%% %7llums %3u | %.4f\n",
                   dp, mp,
                   sr.fm50.dr*100,    (long long)sr.fm50.air_ms,    sr.fm50.total_tx,
                   sr.fm100.dr*100,   (long long)sr.fm100.air_ms,   sr.fm100.total_tx,
                   sr.chain20.dr*100, (long long)sr.chain20.air_ms, sr.chain20.total_tx,
                   sr.grid5x5.dr*100, (long long)sr.grid5x5.air_ms, sr.grid5x5.total_tx,
                   sr.score);

            if (csv) {
                fprintf(csv, "%d,%d,%.4f,%.1f,%lld,%u,%.4f,%.1f,%lld,%u,%.4f,%.1f,%lld,%u,%.4f,%.1f,%lld,%u,%.4f\n",
                        dp, mp,
                        sr.fm50.dr, sr.fm50.lat_ms, (long long)sr.fm50.air_ms, sr.fm50.total_tx,
                        sr.fm100.dr, sr.fm100.lat_ms, (long long)sr.fm100.air_ms, sr.fm100.total_tx,
                        sr.chain20.dr, sr.chain20.lat_ms, (long long)sr.chain20.air_ms, sr.chain20.total_tx,
                        sr.grid5x5.dr, sr.grid5x5.lat_ms, (long long)sr.grid5x5.air_ms, sr.grid5x5.total_tx,
                        sr.score);
            }

            results.push_back(sr);
        }
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
              [](const SweepResult& a, const SweepResult& b) { return a.score > b.score; });

    printf("\n\n=== TOP 10 COMBINATIONS BY COMPOSITE SCORE ===\n");
    printf("%-6s %-8s | FM50 dr%% | FM100 dr%% | Chain dr%% | Air vs DEFAULT | SCORE\n", "DENSE", "MEDIUM");
    printf("%s\n", std::string(90, '-').c_str());

    int shown = 0;
    for (auto& r : results) {
        if (shown++ >= 10) break;
        float fm50_air_pct  = (float)r.fm50.air_ms  / (float)def_result.fm50.air_ms  * 100.0f - 100.0f;
        float fm100_air_pct = (float)r.fm100.air_ms / (float)def_result.fm100.air_ms * 100.0f - 100.0f;
        printf("%-6d %-8d | %7.1f%% | %8.1f%% | %8.1f%% | FM50:%+5.0f%% FM100:%+5.0f%% | %.4f\n",
               r.dense_pct, r.medium_pct,
               r.fm50.dr*100, r.fm100.dr*100, r.chain20.dr*100,
               fm50_air_pct, fm100_air_pct,
               r.score);
    }

    printf("\n=== WINNER ===\n");
    if (!results.empty()) {
        auto& w = results[0];
        printf("  DENSE_PCT  = %d\n", w.dense_pct);
        printf("  MEDIUM_PCT = %d\n", w.medium_pct);
        printf("  SPARSE_PCT = 100 (fixed)\n");
        printf("\n  FM50:    dr=%.1f%%, airtime=%lldms (vs DEFAULT %lldms, %+.0f%%)\n",
               w.fm50.dr*100, (long long)w.fm50.air_ms, (long long)def_result.fm50.air_ms,
               (float)w.fm50.air_ms/(float)def_result.fm50.air_ms*100-100);
        printf("  FM100:   dr=%.1f%%, airtime=%lldms (vs DEFAULT %lldms, %+.0f%%)\n",
               w.fm100.dr*100, (long long)w.fm100.air_ms, (long long)def_result.fm100.air_ms,
               (float)w.fm100.air_ms/(float)def_result.fm100.air_ms*100-100);
        printf("  Chain20: dr=%.1f%%, airtime=%lldms (vs DEFAULT %lldms, %+.0f%%)\n",
               w.chain20.dr*100, (long long)w.chain20.air_ms, (long long)def_result.chain20.air_ms,
               (float)w.chain20.air_ms/(float)def_result.chain20.air_ms*100-100);
        printf("  Grid5x5: dr=%.1f%%, airtime=%lldms (vs DEFAULT %lldms, %+.0f%%)\n",
               w.grid5x5.dr*100, (long long)w.grid5x5.air_ms, (long long)def_result.grid5x5.air_ms,
               (float)w.grid5x5.air_ms/(float)def_result.grid5x5.air_ms*100-100);
    }

    printf("\nCSV written to sweep_results.csv\n");
    if (csv) fclose(csv);
    return 0;
}
