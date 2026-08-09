# CLI Commands

This document provides an overview of CLI commands which are specific to the MU firmware version.

# Navigation

- [Wifi Companion Configuration via Rescue CLI](#wifi-companion-configuration-via-rescue-cli)
- [View or change the maximum direct-route resend attempts](#view-or-change-the-maximum-direct-route-resend-attempts)
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
