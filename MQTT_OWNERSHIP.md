# MQTT Bridge Cross-Core Ownership Model

This document is the Phase 4 deliverable from `STABILITY_TESTABILITY_HANDOFF.md`:
it records **one owner for each mutable runtime domain**, maps every place a
non-owner reads owned state across cores today, and states the target primitive
for each. It is paired with the fork-owned lifecycle test seam
(`src/helpers/MQTTLifecycle.h`, `test/test_mqtt_lifecycle/`).

**Status:** ownership model documented and the lifecycle/teardown test seam
landed (Phase 4). **Phase 5 (branch `phase5/cooperative-mqtt-shutdown`) has now
implemented the cooperative shutdown and the OTA barrier — hazard §4 below.**
Still **deferred** (carried to Phase 5b / Phase 6): publishing a plain-data
snapshot and repointing the §1/§2 consumers, and replacing the §3 `volatile`
handshakes with a command channel. This document is the plan; §1–§3 still
describe current behavior, while §4 is now resolved (see its note). Line
references are against the tree at the time of writing and should be re-verified
before editing.

## Execution contexts

- **MQTT task — Core 0** (`"MQTTBridge"`, `xTaskCreatePinnedToCore(..., MQTT_TASK_CORE=0)`,
  entry `mqttTask` → `mqttTaskLoop`, `MQTTBridge.cpp:923`, `:991`). Owns all
  WiFi / MQTT / NTP I/O and every mutation of `_slots[]`, connection state, and
  NTP state.
- **Loop task — Core 1** (`MyMesh::loop()`). Runs the CLI, WebConfig `tick()`,
  and `AlertReporter::onLoop()`. `examples/simple_repeater/MyMesh.cpp:1529`
  notes the bridge loop is *not* called here — it lives on Core 0.
- **Producer / radio context.** Stages raw radio bytes before queue handoff
  (`storeRawRadioData` → `_staged_*`, consumed by `queuePacket`; both Core 1, in
  guaranteed sequence, `MQTTBridge.h:232-239`).
- **Async TCP context.** WebConfig request parsing and immutable response
  handoff only.

## Ownership model (one owner per mutable domain)

| Domain | Owner | Notes |
|--------|-------|-------|
| MQTT clients, slot connection state, publish counters, packet drain, NTP I/O | **MQTT task (Core 0)** | `_slots[]`, `PsychicMqttClient`s, `_ntp_client`, `_last_raw_*` |
| CLI execution, preference persistence, WebConfig batch draining, bridge lifecycle requests | **Loop task (Core 1)** | issues start/stop/reconfigure, drains the WebConfig batch |
| Packet staging before queue handoff | **Producer / radio (Core 1)** | `_staged_*`, no lock needed (sequential) |
| Request parse + immutable response | **Async TCP** | must not touch mutable bridge state |

The rule that follows: **loop/WebConfig/CLI/AlertReporter code must not directly
inspect mutable MQTT slot objects or client counters.** The MQTT task must
publish a plain-data snapshot they can read instead.

## Current cross-core hazards (to resolve in Phase 5)

All of the following run on **Core 1** and read state mutated by **Core 0**
without a lock or a published snapshot.

### 1. Diagnostic reads of live `_slots[]` via the singleton

Four `static` accessors reach the live object through the file-scope
`s_mqtt_bridge_instance` (`MQTTBridge.cpp:201`, set at `begin()` end `:849`,
nulled first in `end()` `:858` — a plain, non-atomic pointer):

- `getSlotStatusSnapshot` (`:299`) — despite its name, built on demand from
  live `_slots[slot_index]`. **Refined premise:** the returned `name`/`state`
  `const char*`s point at static rodata (string literals / `MQTT_PRESETS[]`),
  so they are *not* dangling. The real hazards are (a) reading the
  `slot.preset` **pointer value**, which Core 0 can null/reassign mid-read
  (`applySlotPreset`, `teardownSlot`), and (b) `slot.client->getPublishOk()`,
  a live client pointer Core 0 can `delete` during teardown (UAF window).
- `formatMqttStatusReply` (`:207`), `formatMqttStatsReply` (`:263`),
  `formatSlotDiagReply` (`:397`) — same singleton + live `_slots[]` reads.

Consumers: CLI (`CommonCLI_Observer.cpp:726, :728, :791`) and the app-layer
stats JSON (`examples/simple_repeater/MyMesh.cpp:1423`, mirrored in
`examples/simple_room_server/MyMesh.cpp:1050`). **Refined premise:**
`WebConfigServer.cpp` itself reads only a compile-time constant
(`getMaxActiveSlots`, `:479`); the live-bridge web reads are in `buildStatsJson`
in the `MyMesh.cpp` app layer.

### 2. Instance reads that survive `end()`

`AlertReporter` (Core 1, `onLoop`, `AlertReporter.cpp:208`) reads live `_slots[]`
via instance methods it reaches through its own `_bridge` pointer
(`AlertReporter.cpp:281` `isSlotEnabledAndAttempted`, `:285`
`getSlotCurrentOutageStartMs`, `:296`/`:311` `getSlotPresetName`). The stats
JSON also calls `bridge->getQueueSize()` (`MyMesh.cpp:1419`).

**Refined premise (teardown hazard):** `end()` nulls only
`s_mqtt_bridge_instance`. The app's `bridge` pointer and `AlertReporter::_bridge`
are **not** cleared by `end()`, so these instance reads can touch a torn-down
bridge. (`getConnectedBrokers()` at `MQTTBridge.cpp:3676` is defined but has
zero consumers.)

### 3. `volatile` cross-core handshakes

Plain `volatile` flags (no atomics/barriers), Core 1 sets the request, Core 0
clears/processes and writes a "done" flag last, Core 1 spins:

- `_slot_reconfigure_pending[]` — set `MQTTBridge.cpp:2057` (Core 1), read/clear
  `:1118` (Core 0).
- `_ntp_force_{requested,done,result}` — request `:3298`, process `:1063-1068`,
  spin-wait `:3308-3315`.
- `_ntp_diag_{requested,done}` (+ non-volatile `_ntp_diag_results[]`) — request
  `:3344`, process `:1073-1076`, spin-wait `:3347`.

The two blocking waiters (`requestForcedNtpSync` `:3298`, `ntpDiag` `:3344`) spin
on Core 1 while `end()` could tear down the singleton/task concurrently.

### 4. Abrupt teardown, no start guard — RESOLVED in Phase 5

Original hazard (retained for context): `end()` used
`vTaskDelete(_mqtt_task_handle)` to kill the task wherever it was (possibly
mid-`_slots[]` mutation or inside mbedTLS), then ran slot/client cleanup *after*
deletion on the caller's context — the OTA teardown heap-panic path. `begin()`
had no double-call guard, and lifecycle state was a single `_initialized` bool.

**Phase 5 resolution** (branch `phase5/cooperative-mqtt-shutdown`):

- `end()` now requests a cooperative stop through `MQTTLifecycle::Coordinator`.
  The MQTT task (Core 0) tears down its own clients where the mbedTLS contexts
  live, acknowledges via `_stop_acked`, and self-terminates; `end()` waits for
  the ack before freeing the queue/buffers. The blind `vTaskDelete` survives
  only as the bounded-timeout fallback, which sets a dirty latch that withholds
  OTA flashing (`canFlashAfterStop()`).
- `begin()` has an idempotent double-call guard and drives the Coordinator to
  `Running`; the lifecycle state now lives in the tested state machine, not a
  bare bool.
- The OTA barrier gates `simple_repeater`'s deferred flash on a clean stop.

Not hardware-validated yet (Phase 7); `MQTT_STOP_TIMEOUT_MS` is a Phase-0
placeholder. Note the residual §1/§2 instance-pointer reads remain deferred, so
consumers can still (as before) touch a torn-down bridge — that is unchanged by
Phase 5 and tracked above.

## Target primitives (Phase 5)

- **Task notifications or a command queue** for one-way lifecycle / reconfigure
  / NTP requests — replacing every `volatile` flag in §3.
- **Immutable published snapshots** for WebConfig, CLI diagnostics, and alerting
  — the MQTT task publishes a plain-data `SlotStatusSnapshot` (owned char
  buffers + scalars, no live pointers) that §1/§2 consumers read. This also
  removes the "instance pointer survives `end()`" hazard because consumers stop
  dereferencing the live bridge.
- **Atomics** only for truly independent scalar state.
- **A mutex** only where ownership transfer or snapshot publication cannot
  express the operation cleanly.

## Lifecycle contract (the test seam)

`src/helpers/MQTTLifecycle.h` encodes the cooperative lifecycle Phase 5 must
implement, as a pure state machine plus a narrow injected `Ops` seam
(clock / task control / resource owner / OTA barrier). The invariants proven by
`test/test_mqtt_lifecycle/` — and that Phase 5's production wiring must preserve:

- `Stopped → Starting → Running → StopRequested → Stopping → Stopped`.
- Idempotent start and stop; safe restart only from `Stopped` (`mayRestart`).
- New connects/publishes/retries/reconfigurations cease once a stop is requested
  (`acceptsNewWork`).
- Resources are released **only** after a stop acknowledgment (or the reviewed
  timeout fallback) — never mid-run.
- A late/stale callback consults `mayTouchOwnedState()` and is a no-op once the
  owner has released resources.
- Bounded stop timeout → reviewed fallback (models replacing the abrupt
  `vTaskDelete`).
- **OTA barrier:** `mayBeginFlash()` is true only after a **clean** stop
  acknowledgment; a timed-out stop leaves flashing blocked so OTA aborts rather
  than writing under uncertain ownership.

### Phase 0 / hardware-pending items

Per the "derive from code + flag" discipline, these are **not** encoded as
constants and must be characterized on hardware before Phase 5 ships:

- The concrete stop timeout (injected as `Coordinator`'s `stop_timeout_ms`;
  must be measured against mbedTLS teardown over `wss` with a down broker).
- Exact callback timing/ordering under a real TLS disconnect.
- Heap / largest-block / task-stack high-water and task/client counts across
  start/stop/restart (Phase 7 gates).

## Deferred to Phase 5 (not done here)

- Publishing the plain-data snapshot and repointing the §1/§2 consumers at it.
- Replacing the §3 `volatile` handshakes with a command queue / task
  notifications.
- The cooperative shutdown state machine in `MQTTBridge` (`end()` rewrite, the
  `begin()` double-call guard) and the OTA teardown barrier.

`MQTTBridge.cpp` was intentionally left untouched in Phase 4 to keep the
merge-sensitive file (≈3.7k lines) free of churn until the Phase 5 change lands
as a single reviewable unit.
