# Flood Suppression

> **Status:** experimental · repeater-only (`simple_repeater`) · branch `feature/flood-suppression-coverage`.
> Not yet merged; still being tuned and measured on hardware.

In a dense mesh every repeater rebroadcasts every flood, so most nodes receive many
redundant copies and the air fills with collisions. **Flood suppression** lets a repeater
cancel its *own* scheduled rebroadcast of a flood when its near neighbours are already
covered — cutting redundant on-air traffic while preserving reach.

See [`cli_commands.md`](cli_commands.md) for the exact command syntax; this document
explains the mechanism and how to tune it.

---

## The decision, in one paragraph

Repeater **M** suppresses its rebroadcast of flood **F** **iff**

1. every **near** neighbour is **covered** (already has F),
2. no **isolated uncovered** near neighbour exists (`must_cover_self`), and
3. the **client-aware** protection gate allows it.

Otherwise M forwards F as normal. Reach is never sacrificed for a neighbour the feature
can see — it only removes rebroadcasts that would be redundant.

---

## Concepts

### Near neighbours — the coverage set
- **Near** = heard recently (`<= 1 h`) **and** link SNR `>= flood.suppress.snr.lo`.
- Coverage is guaranteed only for the **strongest few** near neighbours
  (`NEAR_NEIGHBOUR_COVERAGE_CAP = 5`). A rank-6+ near peer is *not* owed coverage — the
  strongest forwarders cover the most nodes, so guaranteeing only the top few bounds
  airtime while preserving reach. (The adaptive density estimate still counts *all* fresh
  neighbours, so this cap does not weaken the threshold.)
- Inspect live with [`near`](cli_commands.md#near).

### Coverage — how M knows a neighbour already has F
Two sources, combined across every overheard copy of F:

1. **Direct** — the neighbour itself forwarded F (M saw its hash on an overheard path). Certain.
2. **Reach-graph** — the neighbour was *reached* by a near forwarder **fi** via a fresh
   **directed** measured edge `fi -> N` (N heard fi's forward of F). Inferred from the
   reach graph below.

### The reach graph — actively measured, not inferred
- An edge `fi -> N` means *"N can hear fi's transmissions"* (fi reaches N).
- **Directed.** RF links are asymmetric — A hearing B does **not** mean B hears A. The
  graph must not infer forward reach from a reverse observation, or M could suppress a
  rebroadcast and starve a neighbour that is actually deaf toward the forwarder.
- Edges are established by **active TRACE coverage probes** (the earlier design inferred
  them from overheard flood paths, but that stayed empty in sparse/mast topologies where
  two of M's near neighbours never appear consecutively in one flood path).
- **Probe:** a round-trip TRACE with visit-list `[a, b, self]` (2-byte hashes) walks
  `self -> a -> b -> self`. The SNR measured at **b** of **a**'s forward is exactly
  *"does b hear a"*. A new core flag `TRACE_FLAG_TERMINATE_AT_LAST` delivers the result
  back at the initiator instead of a bystander.
- Measured only on demand: when the top-5 set changes (new member / displacement) or after
  a **36 h** refresh. One probe per cadence tick (HW ~60 s) to avoid round-trip collisions.
- An edge is recorded iff the measured SNR `>= flood.suppress.snr.lo`; TTL 36 h.
- **1-hop, not transitive** — "reached by fi" means *could hear fi's specific forward*.

### SNR weighting of overheard forwards
Each overheard neighbour forward counts toward "covered", weighted by the SNR it was
heard at:
- `>= flood.suppress.snr.hi` → counts **double** (a strong/central relay almost certainly reached others too);
- `< flood.suppress.snr.lo` → counts **0** (a marginal relay likely didn't reach the edge — preserve reach).

### Adaptive threshold C (not user-configurable)
The cancellation threshold **C** — how much "already covered" weight is needed before M
suppresses — is **derived from the neighbour table** (an adaptive density estimate: more
near neighbours ⇒ more redundancy required before cancelling) with a static fallback. So a
dense cluster tolerates more redundancy before suppressing; a sparse one stays
conservative. There is no `set flood.suppress.c`.

### Cancel-window widening (`delay.factor`)
A flood M would relay centrally (heard at SNR `>= snr.hi`) gets its random TX-delay window
multiplied by `(1 + flood.suppress.delay.factor)`. A wider window gives a redundant
rebroadcast more time to be observed and cancelled before it is transmitted.

### Client-aware protection (always on)
Suppression must never starve an **attached leaf client** (a companion/sensor/room-server
for which M is the first hop) of a flood it needs. A 3-tier gate is **always active**
(there is no "empty client set ⇒ suppress everything" fallback):

| Tier | Payload types | Behaviour |
|------|---------------|-----------|
| **A** | TRACE, CONTROL | Pure infrastructure → **suppress OK** |
| **B** | ADVERT, GRP_*, ACK, MULTIPART, … | Broadcast, can't address-check → **never suppress** (always forward) |
| **C** | REQ, RESPONSE, TXT_MSG, PATH, ANON_REQ | Addressed → suppress iff destination is **not** an attached client |

The attached-client set (16 slots, ~24 h TTL) is seeded from count-0 addressed packets and
non-repeater adverts. Inspect with [`clients`](cli_commands.md#clients).

### Cold-start safety
Before any TRACE completes, the reach graph is empty ⇒ uncovered peers trigger
`must_cover_self` ⇒ **M forwards everything**. No starvation, no regression vs. firmware
without the feature. Suppression only begins once real reachability has been measured.

---

## When it helps vs. when it is inert
- **Dense omni cluster** (near neighbours mutually in range): edges populate ⇒ redundant
  rebroadcasts cancelled ⇒ large airtime/collision reduction.
- **Sparse / linear / hub-spoke / mast** (near neighbours don't hear each other): the
  graph is **correctly empty** ⇒ little graph-based suppression, because M must cover each
  spoke itself. This is the safe, intended behaviour — the feature never infers
  reachability it has not measured.
- Even with an empty graph, the **direct-coverage** path can still suppress: once M has
  overheard *every* near neighbour forward F, `allNearNeighboursCovered` fires and the
  rebroadcast is cancelled — no reach edge required.

---

## CLI summary

| Command | Effect |
|---------|--------|
| `get/set flood.suppress <on\|off>` | Master switch. `get` also prints the suppression ratio (`suppressed a/b (p%)`). |
| `get/set flood.suppress.snr.hi <dB>` | SNR `>=` this counts double (`-30..30`, default `9`). |
| `get/set flood.suppress.snr.lo <dB>` | SNR `<` this counts 0; also the near-set floor and reach-edge floor (`-30..30`, default `0`). |
| `get/set flood.suppress.delay.factor <n>` | Cancel-window multiplier for central relays (`0..8`, default `2`). |
| `get/set trace.tx.power <dBm>` | TX power for coverage TRACE probes only (`-9..30`, default `10`). |
| `near` | Near coverage peers (strongest first) + TRACE probe health (`meas sent/ret/edge/tmo`). |
| `reach <hash>` | Directed reach edges of one near repeater. |
| `clients` | Attached leaf clients. |

Full syntax and output formats: [`cli_commands.md`](cli_commands.md#flood-suppression-coverage-repeater-only).

---

## Tuning & troubleshooting

### Read the state first
- `near` → the near set and the `meas sent=… ret=… edge=… tmo=…` probe-health line.
- `reach <hash>` → whether measured directed edges exist for a given peer.
- `get flood.suppress` → on/off + the live suppression ratio.

### `reach` is empty on hardware, but `near` shows peers?
The coverage-probe TX power is lowered to `trace.tx.power` (default **10 dBm**) **only on
the initiator**. The probe's first hop (`self -> a`) then runs at reduced power; for a
marginal near neighbour (admitted at a low `snr.lo`) that hop can drop below margin, so the
probe never reaches `a`, no round trip completes, and no edge is recorded. The simulator
ignores TX power, so this only manifests on hardware.

**Fix:** `set trace.tx.power 20` (match normal TX power) and re-check `near` — the `meas`
line's `ret` should rise and edges appear in `reach`. Reading the `meas` line:

| `meas` reading | Meaning |
|----------------|---------|
| `sent=0` | No `>= 2` near-neighbour window yet, or `flood.suppress off`. |
| `sent>0 ret=0` | Probes go out but no round trip completes — loss/collisions, or the first hop failing (see `trace.tx.power`). |
| `ret>0 edge=0` | Round trips complete but the measured link is below `snr.lo` — genuinely weak / no inter-neighbour reachability. |
| `ret>0 edge>0` | Edges recorded — `reach` should list them. |

### Near set churning (peers blink in/out)
At a low `snr.lo` (e.g. `0`), marginal neighbours keep crossing the threshold. Raise
`snr.lo` (e.g. `6`–`10`) to focus coverage on the stable, strong core — fewer, stabler
near peers, faster graph convergence.

### Suppressing too little / too much
- Too little: lower `snr.hi`, or raise `delay.factor` so more redundant rebroadcasts are
  observed in time.
- Too much / worried about reach: raise `snr.lo` (stricter near set), or `set flood.suppress off`.

---

## How it is wired (files)
- `src/helpers/NeighbourLinkTable.h` — the directed reach-graph (`addEdge`/`hasEdge`/`purge`, ring 128, 36 h TTL). `hasEdge` is width-tolerant (prefix match); writes are exact-width.
- `src/helpers/FloodSuppression.h` — per-flood entry with `covered` set, `must_cover_self`, cancel + wait-window.
- `src/Mesh.cpp` + `src/Packet.h` — `TRACE_FLAG_TERMINATE_AT_LAST`: delivers a coverage TRACE back at its initiator (the one core change).
- `examples/simple_repeater/MyMesh.{h,cpp}` — the coverage test (`logRx`), `stepCoverageMeasurement()` (active TRACE scheduling, from `loop()`), `onTraceRecv` (records edges), the always-on 3-tier `clientProtectionAllowsSuppress`, the `near`/`reach`/`clients` reply formatters, and the `trace_tx_power_dbm` burst handling.
- `src/helpers/CommonCLI.{h,cpp}` + `NodePrefs` — the CLI commands and persisted prefs above.

---

## Honest limits
- Coverage is guaranteed only for the **top-5** near neighbours.
- **Invisible neighbours** (asymmetric, absent from M's table) cannot be protected by any
  table-based method. `set flood.suppress off` (or manual per-neighbour exclusion) remains
  the safety net.
- Reach is inferred from a *historically measured* edge ⇒ a small false-positive risk if
  an edge has since gone stale, mitigated by fresh-edge-only use + 36 h TTL + cold-start safety.
