#pragma once
#include <cstdint>
#include <deque>
#include <unordered_set>

// ---------------------------------------------------------------------------
// DensityEstimator — passive neighbor density measurement.
//
// Observes packets received directly (hop_count == 1) over a rolling time
// window and reports how many unique senders the node can hear. No extra
// protocol messages required — purely derived from normal traffic.
//
// Thresholds are configurable at build time:
//   DENSITY_WINDOW_MS    — observation window (default: 60 000 ms)
//   DENSITY_SPARSE_MAX   — neighbor count <= this → SPARSE  (default: 4)
//   DENSITY_DENSE_MIN    — neighbor count >= this → DENSE   (default: 15)
//   Between the two thresholds → MEDIUM
//
// Usage (SimNode):
//   density.observe(src_addr, hop_count, now_ms);   // in logRx()
//   auto tier = density.tier(now_ms);
// ---------------------------------------------------------------------------

#ifndef DENSITY_WINDOW_MS
#define DENSITY_WINDOW_MS   60000u
#endif

#ifndef DENSITY_SPARSE_MAX
#define DENSITY_SPARSE_MAX  4
#endif

#ifndef DENSITY_DENSE_MIN
#define DENSITY_DENSE_MIN   15
#endif

namespace sim {

enum class DensityTier { SPARSE, MEDIUM, DENSE };

class DensityEstimator {
public:
    // Call from logRx() for every received packet.
    // src_addr: any unique identifier for the sending node (e.g. node index or hash).
    // hop_count: only hop_count == 1 packets count toward density.
    void observe(uint32_t src_addr, uint8_t hop_count, uint64_t now_ms) {
        if (hop_count != 1) return;
        _events.push_back({ src_addr, now_ms });
        evict(now_ms);
    }

    // Unique direct-neighbor count seen within the window.
    int neighborCount(uint64_t now_ms) {
        evict(now_ms);
        std::unordered_set<uint32_t> seen;
        for (auto& e : _events) seen.insert(e.src_addr);
        return (int)seen.size();
    }

    DensityTier tier(uint64_t now_ms) {
        int c = neighborCount(now_ms);
        if (c <= DENSITY_SPARSE_MAX)  return DensityTier::SPARSE;
        if (c >= DENSITY_DENSE_MIN)   return DensityTier::DENSE;
        return DensityTier::MEDIUM;
    }

private:
    struct Event { uint32_t src_addr; uint64_t ts_ms; };
    std::deque<Event> _events;

    void evict(uint64_t now_ms) {
        uint64_t cutoff = now_ms > DENSITY_WINDOW_MS ? now_ms - DENSITY_WINDOW_MS : 0;
        while (!_events.empty() && _events.front().ts_ms < cutoff) {
            _events.pop_front();
        }
    }
};

}  // namespace sim
