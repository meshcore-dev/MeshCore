# Restoring accidentally-reverted upstream features

## Background

On 2026-03-20, commit `22eb9b87` — *Revert "Merge remote-tracking branch 'origin/dev' into mqtt-bridge-implementation"* — reverted an entire upstream merge to escape a bad merge state, deleting 860 lines across 66 files. That was not intentional feature removal; it wholesale dropped a batch of upstream progress. When upstream was later re-merged, some collateral came back (MicroNMEA `claim()/release()`, the GAT562 board) but several upstream features were never reconciled and remained missing.

This branch restores them. They are **pure upstream code the fork accidentally dropped**, so restoring re-aligns the fork with upstream and *reduces* the future merge-conflict surface rather than adding to it.

## Phase 1 — duty-cycle enforcement (done on this branch)

Restored the token-bucket duty-cycle enforcement and its cluster:

- `src/Dispatcher.{h,cpp}` — restored upstream's `updateTxBudget()`, `tx_budget_ms`,
  `duty_cycle_window_ms`, `getRemainingTxBudget()`, `getDutyCycleWindowMs()`, and the
  windowed `checkSend()`/`loop()` budget logic. The fork's MQTT **radio watchdog** was
  re-applied on top as pure additions (behind `#ifdef WITH_MQTT_BRIDGE`).
- `src/helpers/StaticPoolPacketManager.{h,cpp}` — restored `getOutboundTotal()` and the
  `0xFFFFFFFF` count-all sentinel in `countBefore()`.
- `src/helpers/StatsFormatHelper.h` — restored the `getOutboundTotal()` call; kept the
  fork's `formatRadioDiag` template.

Net effect: these files now diverge from upstream by **watchdog additions only (0 deletions)**
instead of rewriting upstream's TX logic.

### Why this is safe (no reinterpretation of stored settings)

`getAirtimeBudgetFactor()` changed *meaning* between fork and upstream, but the resulting
duty cycle is the **same formula**:
- Fork: `next_tx = t · factor` → duty ≈ `1/(1+factor)`
- Upstream: `duty = 1/(1+factor)` enforced over a rolling window

So a device's stored `airtime_factor` (a `NodePrefs` field, unchanged) keeps its meaning.
The only behavioral difference is upstream enforces it over a 1-hour window (allowing
short bursts) instead of rigid per-packet spacing — a strict improvement, and the
mechanism that keeps EU 868 MHz nodes under the legally-mandated duty cycle.

### Must be validated on-device before merge

This touches core TX timing. Confirm on hardware — **all device-confirmed 2026-07-09/10
on a Heltec V4.2 (busy live mesh + off-frequency bench):**
- ✅ Normal traffic still flows (repeater forwards, observer uplinks).
- ✅ Sustained TX is throttled to the configured duty cycle: at `set dutycycle 1` on a
  busy mesh, TX pinned to ~1% while MQTT capture continued at full rate (after the
  `RxReservePacketManager` fixes below). Remote admin remains usable under throttle
  with the priority-shed + stale-expiry policy.
- ✅ CAD enabled under load: TX, capture, and CLI all normal.
- ✅ The observer radio watchdog fires and recovers non-destructively: with
  `radio.watchdog 1` on a silent frequency, `err_flags` bit 8 set, radio remained in
  RX state through repeated recovery cycles, and packets were still received after.
  Note: firings are invisible on release builds (no MESH_DEBUG) — check
  `stats-radio-diag` `err_flags`; the watchdog arms only after first radio activity
  (`last_active > 0`), so a radio wedged from boot is deliberately not covered.

Remaining before undraft: passive soak at fleet-default `airtime_factor` (duty
convergence, flat heap, adverts still advertised).

### Known interaction: throttling starves MQTT capture (mitigated)

Found during the on-device duty-cycle load test (`set dutycycle 1`): queued
retransmissions hold static-pool packets with no expiry, so heavy throttling parks the
whole pool in the send queue. `Dispatcher::checkRecv()` then drops received packets
before `logRx()` runs, capping MQTT capture at the TX rate (each TX frees one packet
for one RX). Parked repeats also absorb every budget refill, starving the node's own
CLI responses — device-confirmed: a 1%-duty node on a busy mesh became
un-administrable within ~2 minutes. This is inherent upstream behavior — the old
fork's `next_tx` spacing had the same steady-state drain — but it defeats the
observer's purpose. Mitigated on observer builds by `RxReservePacketManager`
(`src/helpers/RxReservePacketManager.h`): priority-aware shedding below a pool
reserve (own responses/ACKs stay queueable) plus 30 s expiry of stale queued
outbound. See MQTT_INTERNALS.md "Capture vs. duty-cycle throttling".

## Phase 2 — CAD and FEM RX gain (NOT done; needs care + device testing)

Still missing at HEAD, also dropped by `22eb9b87`, still present upstream:

- **`cad_enabled`** — hardware Channel Activity Detection (listen-before-talk) before TX.
  The `Dispatcher`/`Radio` interface (`setCADEnabled`/`getCADEnabled`) is restored by
  Phase 1, so CAD currently stays **off by default** (unchanged behavior). To make it
  configurable again, restore:
  - `NodePrefs.cad_enabled` field, its CLI get/set, and its persistence in
    `CommonCLI.cpp` (`loadPrefsInt`/`savePrefs`). **Offset care:** this branch's
    `/com_prefs` layout is carefully managed — add `cad_enabled` following the same
    append-and-size-guard pattern used for `rx_boosted_gain`/`flood_max_*`, and add a
    host-side round-trip test (see `scratchpad/migtest`).
  - The `MyMesh::getCADEnabled()` override returning `_prefs.cad_enabled`.
  - `RadioLibWrappers::setCADEnabled()` override so the hardware CAD is actually driven
    (the fork's wrapper currently doesn't override it).
- **`radio_fem_rxgain`** — LoRa front-end-module RX gain. Restore `NodePrefs.radio_fem_rxgain`
  + CLI + persistence (same offset care), plus the per-board FEM wiring reverted across
  ~20 `variants/*/target.cpp` and the `heltec_tracker_v2/LoRaFEMControl.{cpp,h}` files.
  This is board-specific and only affects FEM-equipped hardware.

Phase 2 is lower urgency than duty-cycle enforcement (CAD/FEM are capability gaps, not a
compliance regression) and is best done as its own change with per-board hardware testing.
