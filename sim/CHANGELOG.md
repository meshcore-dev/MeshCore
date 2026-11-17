# Simulation Harness Changelog

## [Unreleased] — Adaptive Routing + TX Power Saving

### Added

#### Core Infrastructure
- **`SimRadio.h`**: Added `tx_power_dbm` field (public, default 20 dBm). Extended `TxCallback` signature to carry `tx_power_dbm` through to the bus. Existing capture-effect collision model (6 dB threshold) unchanged.
- **`SimBus.h`**: `InFlightPacket` now carries `tx_power_dbm`. `deliverPending()` applies `received_snr += (tx_power_dbm - 20.0)` at each receiver — lower TX power shrinks effective SNR without touching the channel model. Added `_tsnode_to_seq` compound-key map `(src_pub_key[0..3] << 32 | advert_ts)` for concurrent-flood tracking; prevents false metric attribution when two senders fire in the same simulated second.
- **`DensityEstimator.h`** (new): Passive sliding-window neighbour density estimator. Counts unique direct senders (hop_count==1) over a 60 s window. Returns SPARSE / MEDIUM / DENSE tier. No protocol messages required. Uses `std::unordered_set<uint32_t>` (no fixed cap).
- **`RoutingStrategies.h`**: Added `ADAPTIVE` to the `RoutingStrategy` enum. Added `adaptiveRelayPct()`, `adaptiveDelay()`, `hashBasedRelay()` helpers alongside existing `snrWeightedDelay()` and `pathSnrHybridDelay()`.
- **`SimNode.h`**: Added `routing_strategy` field (default `DEFAULT`). Added `DensityEstimator density` member fed from `logRx()`. Added power-save fields (`power_save_enabled`, `power_save_dense_dbm`, `power_save_medium_dbm`, `full_power_dbm`). Added battery energy tracking (`total_tx_energy_mah`, `total_rx_energy_mah`, `total_rx_time_ms`, `total_suppressed`). Added `static float txCurrentMa(float dbm)` and constants `RX_CURRENT_MA`, `IDLE_CURRENT_MA` matching SX1262 datasheet.
- **`scenario_adaptive.cpp`**: Validates ADAPTIVE vs DEFAULT vs PATH_SNR_HYBRID across FullMesh 50, Chain 20, Grid 5×5 at multiple SNR levels. Confirms ADAPTIVE reduces airtime ≈ 65% at equivalent delivery rate.
- **`scenario_concurrent.cpp`**: Tests 2–8 simultaneous flood sources. Confirms ADAPTIVE maintains delivery advantage in grid topologies under concurrent load; documents known limitation in adversarial full-mesh simultaneous-TX edge case.
- **`scenario_longchain.cpp`**: Tests 20-hop linear chains at marginal SNR. Confirms ADAPTIVE stays at SPARSE tier throughout and behaves identically to DEFAULT on chains.
- **`scenario_mixed.cpp`**: Tests ADAPTIVE nodes coexisting with DEFAULT (legacy) nodes. Confirms full interoperability with no delivery regression.
- **`scenario_dutycycle.cpp`**: Tests EU 1% duty cycle enforcement (`getAirtimeBudgetFactor()`). Confirms ADAPTIVE requires ≈ 40% fewer TX budget slots than DEFAULT.
- **`scenario_relay_pct_sweep.cpp`**: Grid sweep of DENSE% × MEDIUM% relay percentages across FM50, FM100, Chain20, Grid5×5. Identifies optimal operating point (DENSE=15%, MEDIUM=25%). Outputs CSV + ranked table.
- **`scenario_txpower.cpp`** (new): Tests adaptive TX power reduction at CONSERV / MODERATE / AGGRESSIVE levels across FM50, DC6x6 dense positional cluster, and CH20 chain. Measures delivery rate, latency, and per-node energy consumption. Includes 48-hour festival weekend battery projection.
- **`ADAPTIVE_ROUTING.md`** (new): Full design rationale, configuration reference, and empirical results.

### Fixed
- **`DensityEstimator`**: Replaced fixed `seen[64]` array with `std::unordered_set<uint32_t>` — eliminated silent 64-neighbour cap that could misclassify dense networks as MEDIUM.
- **`SimBus::onTransmit`**: Added `if (len > 255) len = 255` bounds guard before `memcpy` into 255-byte `InFlightPacket` buffer.
- **`SimNode::getRetransmitDelay`**: Fixed redundant TX power reset condition (`!power_save_enabled || strategy != ADAPTIVE` → `strategy != ADAPTIVE`).
- **`SimNode::getRetransmitDelay`**: Fixed p_forward gate floating-point edge: `>=` → `>` so `p_forward=1.0` never silently drops a relay.
- **`scenario_concurrent.cpp`**: Fixed stale pointer `ConcurrentResult* def_base = &group.back()` (invalidated by subsequent `push_back`) — replaced with value copy + `bool have_def` flag.
- **`scenario_relay_pct_sweep.cpp`**: Updated `addSweepNode` TxCallback from 4-arg to 5-arg to match updated `SimRadio` signature. Updated `on_recv` lambda to use `_tsnode_to_seq` compound key instead of old `_ts_to_seq`.
- **`scenario_adaptive.cpp`**: Removed unused variable `last_suppressed`.

### Changed
- `SimBus` protected members promoted from `private:` to `protected:` to allow `SweepBus` subclass access in `scenario_relay_pct_sweep.cpp`.
- `adaptiveRelayPct()`: DENSE raised 10% → 15%, MEDIUM raised 20% → 25% — improves resilience under concurrent multi-source floods without increasing airtime in the single-source case.
