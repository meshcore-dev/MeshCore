#pragma once
#include <cstdint>
#include <cmath>
#include "DensityEstimator.h"

// -------------------------------------------------------------------------
// RoutingStrategies.h — sim-only routing strategy definitions.
//
// These override getRetransmitDelay() in SimNode (which already overrides
// mesh::Mesh). Strategies are selected via a per-node enum field.
//
// NOT for the firmware src/ tree — these go through sim validation first.
// -------------------------------------------------------------------------

namespace sim {

enum class RoutingStrategy {
  DEFAULT,        // MeshCore stock: random backoff scaled by airtime
  SNR_WEIGHTED,   // Shorter delay for better SNR; best node relays first
  PATH_SNR_HYBRID,// Path length priority + SNR weighting combined
  ADAPTIVE        // Density-aware: switches strategy + p_forward per tier
};

// -------------------------------------------------------------------------
// SNR quality factor: maps SNR (dB) to [0.0, 1.0]
//   SNR <= -10 dB  → 0.0 (worst link)
//   SNR >= +15 dB  → 1.0 (best link)
// -------------------------------------------------------------------------
inline float snrQualityFactor(float snr) {
  const float SNR_MIN = -10.0f;
  const float SNR_MAX =  15.0f;
  if (snr <= SNR_MIN) return 0.0f;
  if (snr >= SNR_MAX) return 1.0f;
  return (snr - SNR_MIN) / (SNR_MAX - SNR_MIN);
}

// -------------------------------------------------------------------------
// Compute retransmit delay for SNR_WEIGHTED strategy.
//
//   base_delay  = airtime * 52/50 / 2   (same as DEFAULT base)
//   quality     = snrQualityFactor(snr)
//   delay       = base_delay * (1.0 - quality * 0.8)
//               + random jitter [0, 0.2 * base_delay]
//
// Best-SNR node gets up to 80% delay reduction. Small random component
// [0-20% of base] prevents simultaneous transmissions at equal SNR.
// -------------------------------------------------------------------------
inline uint32_t snrWeightedDelay(uint32_t base_delay, float snr, uint32_t rand_val) {
  float q = snrQualityFactor(snr);
  float scale = 1.0f - q * 0.8f;                       // [0.2, 1.0]
  float jitter = (float)(rand_val % 100) / 100.0f * 0.2f * (float)base_delay;
  uint32_t d = (uint32_t)((float)base_delay * scale + jitter);
  return d;
}

// -------------------------------------------------------------------------
// Compute retransmit delay for PATH_SNR_HYBRID strategy.
//
//   path_factor  = 1.0 / (1.0 + path_hops)   shorter path → higher priority
//   quality      = snrQualityFactor(snr)
//   combined     = (path_factor + quality) / 2   [0, 1]
//   delay        = base_delay * (1.0 - combined * 0.85)
//               + random jitter [0, 0.15 * base_delay]
//
// Nodes with short paths AND good SNR relay first. Jitter is tighter than
// SNR_WEIGHTED to sharpen the path-priority signal.
// -------------------------------------------------------------------------
inline uint32_t pathSnrHybridDelay(uint32_t base_delay, float snr, uint8_t path_hops, uint32_t rand_val) {
  float path_factor = 1.0f / (1.0f + (float)path_hops);   // (0, 1]
  float q = snrQualityFactor(snr);
  float combined = (path_factor + q) * 0.5f;               // [0, 1]
  float scale = 1.0f - combined * 0.85f;                   // [0.15, 1.0]
  float jitter = (float)(rand_val % 100) / 100.0f * 0.15f * (float)base_delay;
  uint32_t d = (uint32_t)((float)base_delay * scale + jitter);
  return d;
}

// -------------------------------------------------------------------------
// Compute retransmit delay for ADAPTIVE strategy.
//
// Tier dispatch:
//   SPARSE  → SNR_WEIGHTED   (relay selection matters; minimize hops)
//   MEDIUM  → PATH_SNR_HYBRID (balanced path + SNR priority)
//   DENSE   → long random backoff, p_forward already gates most relays
//
// The DENSE backoff spreads remaining transmitters out so they don't
// collide. Scale factor [2.0, 4.0] × base gives each surviving relay
// a long, well-separated window.
// -------------------------------------------------------------------------
inline uint32_t adaptiveDelay(uint32_t base_delay, DensityTier tier,
                               float snr, uint8_t path_hops, uint32_t rand_val) {
  switch (tier) {
    case DensityTier::SPARSE:
      return snrWeightedDelay(base_delay, snr, rand_val);

    case DensityTier::MEDIUM:
      return pathSnrHybridDelay(base_delay, snr, path_hops, rand_val);

    case DensityTier::DENSE:
      // Survivors (passed p_forward gate) use normal random backoff.
      // Don't inflate delay here — that only extends channel occupancy time
      // without reducing it. Suppression in logRx() handles the real cull.
      return (uint32_t)(rand_val % 5) * base_delay;
  }
  return base_delay;
}

// -------------------------------------------------------------------------
// Hash-based relay selection — deterministic, tunable, zero-coordination.
//
// Instead of a random roll (which has per-flood variance), we use:
//   hash(packet_seed XOR node_seed) % 100 < relay_pct
//
// This gives exactly `relay_pct`% of nodes forwarding any given packet,
// consistently across all floods. Each node independently derives the same
// result without communicating.
//
// relay_pct:
//   SPARSE:  100 (all forward — every relay matters in sparse topology)
//   MEDIUM:   25 (enough redundancy; cuts ~75% of surplus relays)
//   DENSE:    15 (minimal relay set; 85% suppression; handles 2+ concurrent floods)
// -------------------------------------------------------------------------
inline bool hashBasedRelay(uint32_t packet_seed, uint32_t node_seed, int relay_pct) {
  // Simple mix: XOR + multiply-shift hash
  uint32_t h = (packet_seed ^ node_seed) * 0x9e3779b9u;
  h ^= h >> 16;
  return (int)(h % 100) < relay_pct;
}

// Empirically validated via scenario_relay_pct_sweep.cpp + scenario_concurrent.cpp:
//   DENSE=10, MEDIUM=20 → optimal for single-source flooding.
//   DENSE=15, MEDIUM=25 → concurrent-flood resilient, ~85%/75% TX reduction.
//   Below DENSE=10 risks under-coverage in concurrent scenarios (FM50/2-sender: 2% DR).
inline int adaptiveRelayPct(DensityTier tier) {
  switch (tier) {
    case DensityTier::SPARSE: return 100;
    case DensityTier::MEDIUM: return 25;
    case DensityTier::DENSE:  return 15;
  }
  return 100;
}

}  // namespace sim
