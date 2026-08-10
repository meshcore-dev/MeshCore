# CLI Commands

This document provides an overview of CLI commands which are specific to the MU firmware version.

# Navigation

- [Wifi Companion Configuration via Rescue CLI](#wifi-companion-configuration-via-rescue-cli)
- [View or change the maximum direct-route resend attempts](#view-or-change-the-maximum-direct-route-resend-attempts)
- [Flood suppression — redundancy-aware rebroadcast cancellation](#experimental-flood-suppression--redundancy-aware-rebroadcast-cancellation)
- [Flood Suppression Coverage](#flood-suppression-coverage-repeater-only)
  
---
## Wifi Companion Configuration via Rescue CLI

**Description:** Configure Wifi SSID and Password for a ESP32 companion via the rescue command line interface

**Usage:**
- `wifi_ssid <ssid>`
- `wifi_pwd <pwd>`
- `wifi_commit`
- `wifi_clear`

---
## View or change the maximum direct-route resend attempts

**Usage:**
- `get max.resend`
- `set max.resend <value>`

**Parameters:**
- `value`: Maximum number of resend attempts for direct-routed packets (0–3). `0` disables resending entirely.

**Default:** `2`

---

## [Experimental] Flood suppression — redundancy-aware rebroadcast cancellation

**Repeater Only:** Yes

Cancels a repeater's own scheduled flood rebroadcast when neighbouring repeaters have
already forwarded the same flood (i.e. its rebroadcast would be redundant), cutting
on-air flood traffic and collisions while preserving reach. It works alongside a
**coverage test** that tracks which of the repeater's near neighbours have already
received a flood — directly (overheard forwarding) or via a measured inter-neighbour
reach edge — so a rebroadcast is cancelled only when every near neighbour is already
covered.

The cancellation threshold **C is not user-configurable** — it is derived from the
neighbour table (adaptive density estimate) with a static fallback. The options below
are the master switch, the SNR-weighting of overheard forwards, the cancel-window
widening, and the coverage-probe TX power. See
[`README-flood-suppression.md`](README-flood-suppression.md) for the full mechanism,
and the [`near`](#near) / [`reach`](#reach-hash) / [`clients`](#clients) commands to
inspect the learned coverage state at runtime.

**Master switch:**
- `get flood.suppress` / `set flood.suppress <state>`

The `get` reply also reports the suppression ratio, e.g. `> on, suppressed 3/19 (15%)`
(suppressed rebroadcasts / distinct floods heard; `0%` when none heard yet). Returns
plain `> off` when the feature is disabled.

**Parameters:** `state` = `on`|`off` — disables the feature entirely when `off`.

**SNR weighting of overheard forwards:**
- `get flood.suppress.snr.hi` / `set flood.suppress.snr.hi <dB>`
- `get flood.suppress.snr.lo` / `set flood.suppress.snr.lo <dB>`

Each overheard neighbour forward of a flood contributes to the "already covered" count,
weighted by the SNR it was heard at:
- `dB` (`snr.hi`, `-30..30`): heard at SNR `>=` this counts **double** (a strong/central relay almost certainly also reached others).
- `dB` (`snr.lo`, `-30..30`): heard at SNR `<` this counts **0** (a marginal relay likely didn't reach the edge; preserve reach).

`snr.lo` is also the SNR floor for the **near** coverage set and for recording a measured reach edge (see [`near`](#near)).

**Cancel-window widening:**
- `get flood.suppress.delay.factor` / `set flood.suppress.delay.factor <n>`

**Parameters:** `n` = `0..8` — extra TX-delay multiplier applied to a flood this repeater
would relay centrally (heard at SNR `>= snr.hi`). Widening the random delay window gives a
redundant rebroadcast more time to be observed and cancelled before it goes out. `0`
disables the widening.

**Coverage-probe TX power:**
- `get trace.tx.power` / `set trace.tx.power <dBm>`

**Parameters:** `dBm` = `-9..30` — TX power used **only** for the coverage TRACE probes
(the reach-graph measurement), restored to normal afterwards. Near links are strong, so
the default lowers power to reduce disturbance. If `reach` stays empty on hardware despite
near neighbours being present, raise this to the normal TX power (e.g. `set trace.tx.power 20`)
so the probe's first hop reaches marginal near neighbours — see the tuning notes in
[`README-flood-suppression.md`](README-flood-suppression.md).

**Defaults:** `flood.suppress` = `on` · `flood.suppress.snr.hi` = `9` · `flood.suppress.snr.lo` = `0` · `flood.suppress.delay.factor` = `2` · `trace.tx.power` = `10`

**Note:** _Experimental feature_ on branch `feature/flood-suppression-coverage` — still being tuned and measured on hardware.

---

## Flood Suppression Coverage (Repeater Only)

Inspection commands for the flood-suppression coverage state — the near-neighbour set, the measured inter-neighbour reach graph, and the attached-client set used by the client-aware protection gate. Output is **byte-minimal** (it travels over LoRa as a REQ→RESPONSE payload), so hashes use the same 4-byte/8-hex prefix as `neighbors`. See [`README-flood-suppression.md`](README-flood-suppression.md) for what these represent and how to tune them.

### near

**Usage:**
- `near`

Lists the **near** coverage peers — fresh (`<= 1 h` since last heard) and link SNR `>= flood.suppress.snr.lo` — strongest SNR first. This is the exact set the coverage test and the active TRACE measurement act on.

**Output:**
```
near snr_lo=<dB> cap=<5> n=<count>
meas sent=<N> ret=<N> edge=<N> tmo=<N>
<HASH>:<secs_ago>:<snr*4>
...
```

- **Header:** the active `snr.lo` cutoff, the coverage cap (only the strongest `cap` peers are owed coverage / actively TRACE-measured), and `n` = current near count.
- **`meas` line:** coverage-TRACE health — `sent` = probe attempts, `ret` = round-trips that returned to this node, `edge` = reach links recorded (returned with SNR `>= snr.lo`), `tmo` = pairs that timed out twice (no link). Reading it: `sent>0 ret=0` ⇒ round trips not completing (loss, collisions, or the first probe hop not reaching a marginal near neighbour — see `trace.tx.power`); `ret>0 edge=0` ⇒ inter-neighbour links exist but are below `snr.lo`; `sent=0` ⇒ no `>= 2` near-neighbour window yet (or `flood.suppress off`).
- **Per-peer lines:** `<HASH>:<secs_ago>:<snr>` where `snr` is `×4` (divide by 4 for dB), same encoding as `neighbors`. Peers beyond the cap are prefixed `~` (near but **not** owed coverage). `-none-` if empty.

---

### reach \<hash\>

**Usage:**
- `reach <hash>`

Shows the **directed reach edges** of one near repeater — the measured inter-neighbour reach graph used to infer coverage.

**Parameters:**
- `hash`: Hex prefix of the neighbour to query, any even length (e.g. the 8-hex prefix printed by `neighbors`/`near`). No argument replies `reach HASH`.

**Output:** two lines:
- Line 1 `<…` — **reached-by**: near neighbours that reach this node (incoming edges).
- Line 2 `>…` — **reaches**: near neighbours this node reaches (outgoing edges).

Endpoints are 8-hex prefixes, comma-separated; `-` when empty. Status words: `notnear` (known neighbour, but not currently near), `unknown` (no matching neighbour), `ambig` (matches more than one near neighbour).

**Note:** Edges are populated by the active TRACE coverage measurement (see [`README-flood-suppression.md`](README-flood-suppression.md)). On a sparse, linear, or hub-spoke mesh where near neighbours don't hear each other, the graph is correctly empty and `reach` shows `<-` / `>-`.

---

### clients

**Usage:**
- `clients`

Lists the **attached leaf clients** — companion/sensor/room-server nodes for which this repeater is the first hop — tracked by the always-on client-aware protection gate (so suppression never starves them of a flood they need).

**Output:** one line per client `<hash>:<age>s`, where `hash` is the learned identity prefix (8-hex when seeded from an advert, 2-hex when seeded from a message src_hash) and `age` is seconds since last seen. `-none-` if empty.

---
