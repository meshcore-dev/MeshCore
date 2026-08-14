# Flood Suppression — Redundancy-Aware Rebroadcast Cancellation

A `simple_repeater` feature that cancels a repeater's **own scheduled flood
re-broadcast when neighbouring repeaters have already forwarded the same flood**
— i.e. when its re-broadcast would be redundant. It cuts on-air flood traffic and
collisions while preserving reach.

It is implemented entirely at the **application layer** (`simple_repeater`); the
core library (`Mesh`, `Dispatcher`, `Packet`) is not modified.

---

## Mechanism

A flood propagates by every repeater re-broadcasting it once. In a dense mesh
many of those re-broadcasts cover nodes that have already received the flood from
someone else — pure redundancy that only consumes airtime and causes collisions.

This feature turns each repeater into a *listener before it transmits*:

1. **One identity per flood.** `Packet::calculatePacketHash` is path-independent
   for floods (it hashes only `payloadType + payload`). So the original, every
   overheard forward, and the node's own scheduled outbound re-broadcast all
   share **one hash**.

2. **Count overheard forwards at RX-arrival time.** In `MyMesh::logRx` — which
   fires after parse but *before* `calcRxDelay`/`queueInbound` (`Dispatcher.cpp`)
   — every received flood copy is attributed to its hash. The first copy is
   recorded; each later copy is an **overheard forward** by a neighbour and
   increments a per-hash counter.

3. **SNR-weighted counter (correct sign).** The weight of each overheard forward
   depends on its RX SNR, which is a proxy for how central vs. edge the node is:
   | RX SNR of the overheard forward | Weight | Meaning |
   |---|---|---|
   | `>= snr.hi` | **+2** | Strong forward nearby → you are central, your rebroadcast is redundant |
   | `< snr.lo`  | **0**  | Weak forward → you are at the edge, keep extending reach |
   | otherwise   | **+1** | Neutral |

4. **Cancel when redundant.** Once the weighted count reaches the threshold **C**,
   the hash entry is flagged `suppressed` and the already scheduled outbound
   re-broadcast is removed from the TX queue (`cancelPendingFloodOutbound` →
   `_mgr->removeOutboundByIdx` + `releasePacket`).

5. **Scheduling gate.** `allowPacketForward` refuses to schedule a rebroadcast
   whose hash is already flagged `suppressed`. This covers the ordering case where
   a later copy arrives and is flagged before the first copy is processed.

6. **TX-delay bias.** `getRetransmitDelay` widens the TX window for central relays
   (RX SNR `>= snr.hi`) so they have more time to observe overheard forwards and
   be cancelled; edge relays keep the short delay and extend reach quickly.

Per-hash bookkeeping lives in a small ring with TTL eviction
(`src/helpers/FloodSuppression.h`), purged from `MyMesh::loop()`.

### Why the counter runs in `logRx` (arrival time)

`allowPacketForward` and `filterRecvFloodPacket` are not suitable counting hooks:
the former is called only for the first copy, the latter runs after the inbound
delay. `logRx` runs for **every** received packet, after parse, **before**
`calcRxDelay` — so overheard forwards are counted the instant they arrive, not
after their own RX delay. This makes the cancellation deadline
`own_TX_fire_time` instead of `own_TX_fire_time − neighbour_calcRxDelay`, i.e.
cancels reliably land before the redundant TX goes out.

---

## Configuration

There is **one master switch** and four tuning parameters. The threshold **C**,
`snr.hi` and `snr.lo` are **not user-configurable** — they are derived from the
neighbour table (adaptive) with static fallbacks (see *Adaptive mode*).

`NodePrefs` fields (`src/helpers/CommonCLI.h`), persisted at file bytes 295–299
(`src/helpers/CommonCLI.cpp`):

| Field | Type | Default | Meaning |
|---|---|---|---|
| `flood_suppress` | `uint8_t` | `1` (on) | **Master switch.** `0` = feature fully off; `1` = on (adaptive + static fallback). |
| `flood_suppress_snr_hi` | `int8_t` (dB) | `9` | Overheard forward with SNR `>=` this counts **double** (adaptive p75; configured value is the fallback). |
| `flood_suppress_snr_lo` | `int8_t` (dB) | `0` | Near-membership threshold; overheard forward with SNR `<` this counts **0** (adaptive p25; configured value is the fallback). |
| `flood_suppress_delay_x` | `uint8_t` | `3` | Extra TX-delay multiplier for central flood relays. |
| `trace_tx_power_dbm` | `int8_t` (dBm) | `10` | TX power for coverage TRACE probes only (lower = less disturbance). |

The feature is **on by default**; `set flood.suppress off` (or YAML
`flood_suppress: 0`) disables it completely.

> **Real-HW note:** adding these trailing bytes changes the persisted prefs binary
> layout. Older prefs files simply leave the fields at the constructor defaults
> (on) — no migration step required.

### CLI (dot-notation)

| Command | Effect |
|---|---|
| `set flood.suppress on` / `off` | master switch (`get flood.suppress`) |
| `set flood.suppress.snr.hi <dB>` | `-30..30` (`get flood.suppress.snr.hi`) |
| `set flood.suppress.snr.lo <dB>` | `-30..30` (`get flood.suppress.snr.lo`); adaptive p25 fallback |
| `set flood.suppress.delay.factor <n>` | `0..8` (`get flood.suppress.delay.factor`) |
| `set trace.tx.power <dBm>` | `-9..30` (`get trace.tx.power`) |

### Channel-state policy: deliberately none

An earlier revision gated suppression on the measured noise floor (a per-site
quiet baseline plus a configurable margin) with a payload-class policy on top.
It was **removed** on purpose. On an active mesh the measured floor mostly
reflects the mesh's **own** redundant traffic, so the gate closed exactly when
suppression was most valuable — a self-reinforcing loop (little suppression →
more forwards → "noisy" → even less suppression). Channel state therefore does
not enter the suppression decision at all: under load a redundant rebroadcast
is itself the load, and cancelling it is the right move even at some residual
delivery risk. The only content-based gate is the always-on 3-tier **client
protection** (`MyMesh::clientProtectionAllowsSuppress`: TRACE/CONTROL free,
addressed types iff the destination is not an attached client, broadcasts that
clients may need are always forwarded).

### SNR-repeat fallback

The coverage-graph test is intentionally conservative: it only suppresses when it
can **prove** every near neighbour already has the flood. When the graph cannot
prove coverage (e.g. the forwarders are beyond the top-N coverage cap, so no
measured TRACE edge exists), a secondary **legacy SNR-repeat counter** still
applies: each overheard forward of the same hash increments a per-flood weighted
counter (`SNR >= snr.hi` → +2, `< snr.lo` → 0, else +1). Once the weighted count
reaches the effective **C**, the rebroadcast is cancelled even without graph
proof. The graph result always wins; the fallback only widens the suppression
set. The same 3-tier client protection applies as on the graph path; channel
state and payload class gate neither path (see *Channel-state policy:
deliberately none*). The fallback is fixed ON and not configurable.

---

## Adaptive mode (self-tuning, zero-admin)

With the master switch **on**, the threshold **C**, `snr.hi` **and `snr.lo`** are
**derived from the repeater's neighbour table** (`simple_repeater`'s `neighbours[]`,
seeded from zero-hop repeater adverts / node-discovery and kept fresh by overheard
forwards), with safe **static fallbacks** when no neighbour data is available. No
per-topology tuning is required.

`MyMesh::updateAdaptiveFloodParams()` runs throttled (~every 1 min) from `loop()`
and caches the **effective** values; the consumption sites read
`effectiveFloodSuppressC()` / `effectiveFloodSuppressSnrHi()` /
`effectiveFloodSuppressSnrLo()`. C/hi/lo are derived under `#if MAX_NEIGHBOURS`
(the table is a build flag).

**Derivation** (only **fresh** neighbours counted — `heard_timestamp` age ≤ 600 s,
i.e. heard within the last 10 min):

A neighbour's `heard_timestamp` is set when it is first learned (zero-hop advert or
node-discovery reply) **and kept current by every overheard forward**: `logRx` calls
`touchNeighbourByHash`, which matches the received flood's *last* path hash — the
immediate RF neighbour that relayed it — against the table and refreshes
`heard_timestamp` plus a smoothed SNR (running mean, x4 fixed-point). This matters
because adverts can be spaced many hours apart (default 47 h, up to ~150 h): without
the activity refresh the whole table would age past 600 s and adaptive would collapse
to the static fallback within 10 min of boot. The refresh can only update
*already-known* neighbours — a forwarded flood carries only a path hash, not a full
identity, so seeding brand-new neighbours still needs an advert / node-discovery.

| Parameter | Derived from | Rule |
|---|---|---|
| `effective_c` | neighbour **density** `n` (fresh count) | `n < 3 → 0` (edge node — don't suppress) · `3–4 → 3` · `≥ 5 → 2` (dense core — aggressive) |
| `effective_snr_lo` | link-SNR **p25** of fresh neighbours | near-membership threshold; `clamp(p25, -5, 15)`; needs ≥ 4 samples, else the configured `snr.lo` |
| `effective_snr_hi` | link-SNR **p75** of fresh neighbours | `clamp(p75, eff.lo+4, eff.lo+12)`; needs ≥ 4 samples, else the configured `snr.hi` |

`snr.lo` does **not** feed back into the density count `n` (which is by timestamp
only), so widening/narrowing the near set cannot oscillate `c`.

A 2-cycle debounce on `c` prevents flapping when the neighbour count fluctuates (at
a 1-min recompute cadence an adopted change lands within ~2 min; the recompute cost
itself is negligible, so the cadence bounds *reaction latency*, not CPU load).

**Static fallback** — when the neighbour table is unavailable the feature still
works with a built-in threshold (`FLOOD_SUPPRESS_FALLBACK_C = 2` in `MyMesh.cpp`)
plus the configured `snr.hi`/`snr.lo`/`delay.factor`:

| Condition | `effective_c` |
|---|---|
| master switch **off** | `0` (feature disabled) |
| master on, ≥ 1 fresh neighbour (adaptive active) | derived from density (above) |
| master on, no fresh neighbours / `MAX_NEIGHBOURS` undefined / cold start | `FLOOD_SUPPRESS_FALLBACK_C` (= 2) |

So a node that knows its neighbourhood adapts (incl. turning off if it is sparse);
a node that does not yet know it (cold start, or no table compiled in) uses the
gentle static fallback. The counter mechanism itself protects genuinely sparse
nodes regardless — too few overheard forwards ever reach the threshold.

### Self-contained boot discovery

Adaptive needs the neighbour table populated soon after boot. Rather than depend on
another feature being enabled, flood suppression brings its **own** boot discovery,
analogous to `feature/repeater-swarm-2`:

- `sendNodeDiscoverReq(uint32_t delay_millis)` accepts a future, jittered send
  (de-synchronises a fleet reboot); `examples/simple_repeater/main.cpp` fires it at
  ~21 s after the boot advert, gated on `flood_suppress`. The table then fills
  within ~30–60 s on hardware.

### Simulator caveat (mcsim)

The neighbour table does **not** populate in the simulator: all repeaters boot
synchronously, so their periodic adverts collide and no one receives them, and the
sim (`sim_main.cpp`) deliberately omits the boot discovery for the same reason.
Consequently adaptive stays on the **static fallback** in sim — which still
demonstrates the suppression effect (see measured result) and verifies the safe
fallback, but the *adaptive* c/hi tuning itself must be measured on hardware (boot
discovery with jitter de-synchronises real reboots). The overheard-forward liveness
refresh (`touchNeighbourByHash`) does **not** change this: it only refreshes
already-known neighbours, and in sim none are ever seeded, so sim still runs on the
static fallback. The refresh is a hardware-only improvement.

---

## Code locations (firmware)

| File | Change |
|---|---|
| `src/helpers/FloodSuppression.h` | **New.** Per-hash ring: `{hash, weighted_count, first_snr, strongest_overheard, first_seen, suppressed, active}` + `find` / `touch` / `purge`. |
| `examples/simple_repeater/MyMesh.h` | Helper include; `_flood_supp` + adaptive state (`_fs_eff_c`, `_fs_eff_hi`, `_fs_eff_lo`, `_fs_adaptive_active`, …); `cancelPendingFloodOutbound`, `updateAdaptiveFloodParams`, `effectiveFloodSuppressC/Hi/Lo`, `touchNeighbourByHash`; `sendNodeDiscoverReq(delay_millis)`. |
| `examples/simple_repeater/MyMesh.cpp` | `logRx` (count + SNR-bias + cancel + neighbour-liveness refresh via `touchNeighbourByHash`), `allowPacketForward` (gate), `cancelPendingFloodOutbound`, `touchNeighbourByHash` (refresh known neighbour from an overheard forward's last path hash + smoothed SNR), `getRetransmitDelay` (delay bias), `loop()` (purge + adaptive recompute @ 1 min), `updateAdaptiveFloodParams` + effective accessors + `FLOOD_SUPPRESS_FALLBACK_C`, `sendNodeDiscoverReq(delay)`, constructor defaults. Consumption reads *effective* values. |
| `examples/simple_repeater/main.cpp` | Boot discovery: `sendNodeDiscoverReq(…)` gated on `flood_suppress`. |
| `src/helpers/CommonCLI.h` / `CommonCLI.cpp` | `NodePrefs` fields + persisted read/write + defaults + `set/get flood.suppress*` CLI handlers. |

`companion_radio`, `simple_room_server` and `simple_sensor` are unaffected — only
`simple_repeater` overrides `logRx`/`allowPacketForward` for suppression.

---

## Simulator integration (mcsim)

The feature is exercised through the simulator, plumbed end-to-end:

- **Properties** `firmware/flood_suppress`, `firmware/flood_suppress_{snr_hi,snr_lo,delay_x}`
  (`crates/mcsim-model/src/properties/definitions.rs`, registered in `registry.rs`,
  re-exported in `mod.rs`, applied in `crates/mcsim-model/src/lib.rs`).
- **Config structs** `RepeaterConfig` (`crates/mcsim-firmware/src/lib.rs`) and the
  FFI `NodeConfig` (`crates/mcsim-firmware/src/dll.rs`) — both gained the four
  fields; `_reserved` shrank 36 → 32 bytes to keep the C ABI identical to
  `SimNodeConfig` (`simulator/common/include/sim_api.h`).
- **Forwarding** in `simulator/repeater/sim_main.cpp`, guarded by
  `SIM_FW_HAS_FLOOD_SUPPRESS`.
- **Feature detection** in `crates/mcsim-firmware/build.rs` — defines the macro
  when `CommonCLI.h` contains a `flood_suppress*` field. The sim build also defines
  `MAX_NEIGHBOURS=50` (matching HW variants) so the neighbour table compiles in sim.

### A/B testing

Topology YAMLs are merged (later overrides earlier), so a tiny overlay toggles the
feature without duplicating the topology:

```yaml
# fsupp_baseline.yaml — feature OFF (unsuppressed baseline)
defaults:
  node:
    firmware:
      flood_suppress: 0
```

```bash
# baseline (off)
cargo run -- run examples/topologies/multi_path.yaml examples/behaviors/broadcast.yaml \
  examples/topologies/fsupp_baseline.yaml \
  --seed 42 --duration 120s --metrics-output json --metrics-file baseline.json \
  --metric mcsim.flood.* --metric mcsim.radio.tx_packets/route_type \
  --metric mcsim.radio.tx_airtime_us/route_type --metric mcsim.radio.rx_collided

# on (default; no overlay needed — or use fsupp_on.yaml to pin the params)
cargo run -- run examples/topologies/multi_path.yaml examples/behaviors/broadcast.yaml \
  --seed 42 --duration 120s ...   # same metrics
```

**Relevant metrics**
- `mcsim.flood.coverage` — gauge `reached_nodes / total_nodes`; the reach signal.
- `mcsim.radio.tx_packets{route_type=flood}` / `mcsim.radio.tx_airtime_us{route_type=flood}` — flood cost.
- `mcsim.radio.rx_collided` — collision count.
- (`mcsim.flood.nodes_reached` is a histogram that mixes channel broadcasts with
  repeater advert floods — treat its tail as noise, not as a reach signal.)

### Measured result (`multi_path.yaml` + `broadcast.yaml`, seed 42, 120 s)

In the sim adaptive stays on the static fallback (`FLOOD_SUPPRESS_FALLBACK_C = 2`),
so this is the fallback-path effect:

| Config | Flood TX | Flood airtime | Collisions | Coverage |
|---|---|---|---|---|
| `off` (baseline) | 237 | 38.0 M | 142 | 0.308 |
| `on` (default) | 163 (**−31 %**) | **−31 %** | 57 (**−60 %**) | 0.308 |

`coverage` is stable at `0.308 = 4/13` (= all four companion recipients reached) —
**reach is preserved**; the reduction is in *redundant copies*, exactly the intent.

---

## Tuning guidance

In adaptive mode `c` is self-tuned, so these mainly adjust the SNR-weighting and
the cancel window (and serve as the static fallback when no neighbour data exists).

- `flood.suppress.snr.hi` is the main aggressiveness lever and should sit in the
  upper portion of the topology's link-SNR range: if almost every link exceeds it,
  every overheard forward counts double and the threshold is reached after a single
  forward (very aggressive → may over-suppress). Raise it to suppress only the
  genuinely redundant, central relays.
- `flood.suppress.snr.lo` should sit below the weakest link you still want to *use*
  for reach, so edge relays are never suppressed by their own weak inbound.
- `flood.suppress.delay.factor` widens the cancel window for central relays
  (higher → more time to observe overheard forwards and be cancelled).
- Monotonic: lower `snr.hi` → more aggressive; higher → gentler.
