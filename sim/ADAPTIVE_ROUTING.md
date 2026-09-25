# Adaptive Routing Simulation — Design & Results

## Overview

This document describes the ADAPTIVE routing strategy added to the MeshCore simulation harness, including the density estimator, hash-based relay selection, relay suppression, and adaptive TX power saving. Empirical validation results across all scenario types are summarised below.

---

## 1. Density Estimator (`DensityEstimator.h`)

Passive, protocol-free neighbour density measurement. Each node counts how many **unique direct senders** it hears within a 60-second rolling window. Only hop_count==1 packets are counted (direct neighbours only — relays don't inflate the estimate).

| Threshold | Tier   | Default value |
|-----------|--------|---------------|
| ≤ DENSITY_SPARSE_MAX | SPARSE | ≤ 4 neighbours |
| ≥ DENSITY_DENSE_MIN  | DENSE  | ≥ 15 neighbours |
| Between               | MEDIUM |                 |

All thresholds are compile-time overridable (`-DDENSITY_WINDOW_MS=30000`, etc.).

**Implementation note:** Uses `std::unordered_set<uint32_t>` for unique-sender counting, eliminating the former fixed-array cap at 64 neighbours.

---

## 2. Hash-Based Relay Gate

In ADAPTIVE mode, only a deterministic subset of nodes relay each flood. Selection is based on a hash of the packet content XOR a per-node seed:

```
selected = hash(packet_seed XOR node_seed) % 100 < relay_pct
```

- `packet_seed`: first 4 bytes of packet payload (stable across all recipients of the same flood)
- `node_seed`: per-node constant — `node_idx × 0x9e3779b9` (in sim); firmware uses pub-key prefix

This is **zero-coordination**: every node independently makes the same relay/suppress decision for a given packet without any signalling. Different packets select different relay subsets, providing stochastic load balancing.

### Relay percentages by density tier

| Tier   | Relay % | Rationale |
|--------|---------|-----------|
| SPARSE | 100%    | Every relay counts; no redundancy to spare |
| MEDIUM | 25%     | Enough redundancy to survive single relay failure |
| DENSE  | 15%     | Flood reaches all neighbours with 7–8 relayers in FM50; collisions reduced ≈ 60–70% vs DEFAULT |

These were derived empirically from `scenario_relay_pct_sweep` across FM50, FM100, Chain20, Grid5×5 topologies.

---

## 3. Relay Suppression

After transmitting or scheduling a relay, if a node overhears another node already successfully relaying the same flood (matched by packet hash), it cancels its own queued outbound. This catches the edge case where two ADAPTIVE-selected relays race.

Implemented in `SimNode::logRx()` via `_mgr_ref.removeOutboundByIdx()`.

---

## 4. Routing Strategies

Four strategies are available via `RoutingStrategy` enum in `RoutingStrategies.h`:

| Strategy | Description |
|----------|-------------|
| `DEFAULT` | Stock MeshCore: random backoff 0–5× base airtime. All nodes relay. |
| `SNR_WEIGHTED` | Nodes with better SNR relay sooner (lower backoff). |
| `PATH_SNR_HYBRID` | SNR-weighted backoff scaled by hop count — far nodes deprioritised. |
| `ADAPTIVE` | Hash-gate + density-aware relay pct + relay suppression. |

The `ADAPTIVE` strategy is the primary contribution. It matches or exceeds DEFAULT delivery rate while reducing total airtime by 60–70% in dense topologies.

---

## 5. Adaptive TX Power Saving

When `power_save_enabled = true`, ADAPTIVE-mode nodes reduce their transmit power based on current density tier:

| Tier   | Default TX power | Example setting | TX current (SX1262) |
|--------|-----------------|-----------------|---------------------|
| SPARSE | 20 dBm (full)   | 20 dBm          | 120 mA              |
| MEDIUM | configurable    | 14 dBm          | 45 mA               |
| DENSE  | configurable    | 10 dBm          | 25 mA               |

TX power affects received SNR at all neighbours via a linear offset: `received_snr += (tx_power_dbm - 20.0)`.

**Capture effect**: reducing TX power in dense areas shrinks the interference radius — nearby nodes still receive well above LoRa sensitivity, while the node no longer stomps on distant clusters. The LoRa capture effect (6 dB threshold in SimRadio) means the stronger local signal always wins over a distant weaker interferer.

### Battery model (SX1262 typical)

```
tx_energy_mah = txCurrentMa(dbm) × airtime_s / 3600
rx_energy_mah = 5.5 mA × airtime_s / 3600
```

TX current lookup (`txCurrentMa()`):
- ≥ 20 dBm → 120 mA
- ≥ 17 dBm → 90 mA
- ≥ 14 dBm → 45 mA
- ≥ 10 dBm → 25 mA
- < 10 dBm → 15 mA

---

## 6. Empirical Results

### 6a. ADAPTIVE vs DEFAULT (FM50, SNR=8dB)

| Metric | DEFAULT | ADAPTIVE |
|--------|---------|----------|
| Delivery rate | 100% | 100% |
| Total TX packets | ~2500 | ~450 |
| Collisions | ~800 | ~120 |
| Airtime reduction | — | ≈ 65% |

### 6b. TX Power Saving (FM50, ADAPTIVE, 20 floods)

| Mode | Dense dBm | Med dBm | Delivery | Energy saving vs FULL |
|------|-----------|---------|----------|-----------------------|
| FULL_PWR  | 20 | 20 | 100% | — |
| CONSERV   | 14 | 17 | 100% | ≈ 20% |
| MODERATE  | 10 | 14 | 100% | ≈ 35% |
| AGGRESSIVE | 7 | 10 | 100% | ≈ 45% |

TX power reduction does **not** hurt delivery in full-mesh or dense positional cluster topologies. Chain topologies (sparse by nature) stay at full power (SPARSE tier = 100% relay, full power always).

### 6c. Long chain (20 hops, SNR=6dB)

PATH_SNR_HYBRID outperforms DEFAULT on marginal multi-hop links (+5–8% DR). ADAPTIVE matches DEFAULT on chains because density stays SPARSE throughout — no relay suppression, all nodes relay at full power.

### 6d. Duty cycle enforcement

At 1% EU duty cycle (`duty_cycle_factor=99`), ADAPTIVE requires ≈ 40% fewer TX slots than DEFAULT, providing significantly more headroom before the duty cycle budget is exhausted.

### 6e. Legacy compatibility (mixed firmware)

Legacy nodes (DEFAULT strategy) interoperate fully. ADAPTIVE nodes relay legacy floods with the same hash gate. Legacy nodes relay ADAPTIVE-originated floods without suppression (they simply forward everything), which provides additional redundancy — no delivery regression observed.

---

## 7. Festival Weekend Battery Estimate

Assumptions: 50-node full-mesh, 1 flood/30s, SF8 BW62.5, SX1262, 2000 mAh battery.

| Mode | TX mAh/hr | RX mAh/hr | Radio total | Est. hours (radio only) |
|------|-----------|-----------|-------------|------------------------|
| FULL_PWR   | 0.373 | 0.114 | 0.487 | ~4100 hrs |
| MODERATE   | 0.078 | 0.114 | 0.192 | ~10400 hrs |
| AGGRESSIVE | 0.046 | 0.114 | 0.160 | ~12500 hrs |

**Note:** MCU idle current (~10–30 mA continuous) dominates in practice, adding ≈ 9.6 mAh/hr at 20 mA. On a 2000 mAh battery this limits runtime to ≈ 100–130 hours regardless of radio power level. TX power reduction remains valuable for: peak current management, heat reduction, and lowering the area RF noise floor during high-density events.

---

## 8. Configuration Reference

```cpp
// SimNode fields (set after addNode(), before run())
node->routing_strategy      = RoutingStrategy::ADAPTIVE;
node->power_save_enabled    = true;
node->power_save_dense_dbm  = 10.0f;   // TX power in DENSE tier
node->power_save_medium_dbm = 14.0f;   // TX power in MEDIUM tier
node->full_power_dbm        = 20.0f;   // TX power in SPARSE tier (and non-ADAPTIVE)
node->duty_cycle_factor     = 99.0f;   // EU 1% duty cycle
node->p_forward             = 1.0f;    // manual relay probability (non-ADAPTIVE only)
```

Compile-time density thresholds (set in CMakeLists.txt or via `-D` flags):
```
DENSITY_WINDOW_MS    (default: 60000)
DENSITY_SPARSE_MAX   (default: 4)
DENSITY_DENSE_MIN    (default: 15)
```
