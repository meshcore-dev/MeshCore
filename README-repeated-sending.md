# Repeated Sending (branch `feature/repeated-sending-2`)

Reliable delivery of direct-routed packets via **cancellable retransmissions**. The feature augments DIRECT/TRACE routing with an automatic, self-limiting retransmission that switches itself off one hop downstream as soon as successful forwarding is detected.

## Problem

A direct-routed packet (DIRECT / TRACE) that travels to its destination via one or more relays is considered "sent" by the originator as soon as it is on air. If it is lost on the first hop (collision, interference, fading), there is no retry at the MeshCore layer — the packet is gone, and the application layer must re-schedule the entire message. Flood traffic does not have this problem because it is widely broadcast anyway; DIRECT packets, however, are the cost-conscious path for targeted messages.

## Solution

After every DIRECT packet is sent, the Dispatcher schedules a retransmission. The key trick: **the originator listens for a downstream relay forwarding the packet**. As soon as it receives that forward (the "forwarding echo"), it knows its packet survived at least the next hop and cancels the pending retransmission. The result is an error-correction mechanism that produces **no** extra packet in the success case, and in the failure case retransmits precisely where the packet actually got stuck.

Flow in detail:

1. **Schedule the resend** – `Dispatcher::resendPacket()` runs after TX only for DIRECT packets with at least one relay hash in the path (`getPathHashCount() > 0`). It does not wait for a timeout; instead it re-queues the packet after a computed **cancel window** `W = C0 + K·airtime + margin + (attempt−1)·jitter`, capped at an airtime-proportional ceiling (`CAP_BASE + CAP_AIRTIME_X·airtime`). The window always outlasts the downstream forward for any packet length or spreading factor (a flat ceiling under-covered SF10 long messages, so every long packet fired a redundant, colliding resend), while staying as short as the airtime allows so the echo arrives before the resend fires.
2. **Detect the forward and cancel** – In `Mesh::onRecvPacket()`, every received DIRECT packet is checked: is it a downstream forward of one of our own pending resends (`Packet::isRetryMatch()`)? If so, the resend is removed from the outbound queue (`removeOutboundByIdx`) — the current `outbound` packet and every queued entry alike. A deterministic per-hop busy-recheck bucket (`pathHashCount % 3`) keeps deferred resenders of the same chain from re-checking the channel on the same loop tick.
3. **Non-invasive LBT** – Resends use *no* hardware CAD, but `isResendChannelActive()`: a pure IRQ/register read (preamble detection + RSSI margin above the noise floor). RX stays open so the forwarding echo is not missed. First sends still use normal CAD (collision avoidance is worth the momentary deafness).

### TRACE handling

TRACE packets append an SNR byte per hop, which changes their hash. `isRetryMatch()` therefore compares payload + SNR-path prefix instead of the hash for TRACE. Because the TRACE branch in `onRecvPacket()` always returns, the generic echo-cancel block is never reached for TRACE — the TRACE branch therefore performs its own cancel scan: when a later hop's forward of a TRACE we already sent is overheard, our queued TRACE resend is dropped (for well-formed, non-terminal legs only). Without this, every intermediate hop would re-send each TRACE up to `max_resend_attempts` times even though the next hop had already forwarded it.

At the final forwarding hop (destination does not forward) the packet is flagged `no_retry` and sent once through the normal TX gate — a retry here could never be cancelled by overhearing a forward, and pre-setting the attempt counter (the earlier approach) made the first send look like a resend: it went through the resend LBT gate instead of the normal channel check and was counted in the resend statistics.

### Final-hop handling

On the final relay hop there is no downstream forward to overhear — the destination does not forward. For **TXT_MSG with ACK** the destination acknowledges receipt with an ACK. This hop is allowed exactly one ACK-cancellable resend (flag `final_hop_ack_resend`). `Mesh::cancelPendingFinalHopResend()` cancels the oldest final-hop resend still sitting in the queue as soon as the ACK returns (FIFO, since ACKs are produced in delivery order). A resend already on air is deliberately not aborted (the destination dedups the duplicate via `wasSeen`).

## Configuration

| Location | Value | Meaning |
|---|---|---|
| CLI `set max.resend <0–3>` / `get max.resend` | Default `2` | Maximum resend attempts for DIRECT packets. `0` disables the feature entirely. `get` also reports the resend ratio, e.g. `2, resends 64/173 (37%)`. Available on CommonCLI nodes (repeater/chat/room-server) **and on companion radios** via their USB/BLE CLI (`CMD_RUN_CLI_COMMAND` passthrough) — identical commands and reply format everywhere. |
| `NodePrefs` (persisted) | `max_resend_attempts` | Saved/loaded via the ConfigSerializer prefs file; sanitised to 0–3. (The legacy binary migration path reads it at byte offset 295.) |
| Companion-radio `CMD_SET_*` frame | Byte 6 in the path block | App-protocol path to set `max_resend_attempts` from the companion. |
| `RESEND_INTERFERENCE_MARGIN` (compile-time) | Default `12` dB | Margin above the noise floor at which a resend is blocked (non-invasive LBT). |
| `RESEND_DEFERRAL_*` (compile-time) | `C0 1500`, `K 2`, `margin 40`, `cap 1500 + 3·airtime`, `jitter 100` | Cancel-window model, in milliseconds/airtime multiples (see `src/Dispatcher.h`). |

## Implementation (core files)

- `src/Dispatcher.cpp` / `.h` – `resendPacket()`, `resendWindowFor()`, `isResendChannelActive()`, `getMaxResendAttempts()`, pool-shedding skip, resend statistics.
- `src/Mesh.cpp` / `.h` – forwarding detection and resend cancellation in `onRecvPacket()` (generic DIRECT block + TRACE branch scan), `cancelPendingFinalHopResend()`, final-hop marking (`final_hop_ack_resend`, `no_retry`).
- `src/Packet.cpp` / `.h` – cached packet hash (`hash`, `hash_hex`), `isRetryMatch()` (TRACE-specific vs. hash-based), fields `sending_attempts` / `no_retry` / `final_hop_ack_resend`.
- `src/helpers/SimpleMeshTables.h` – dedicated ACK dedup table (`_acks[]`, via `ack_crc`), so multi-ACK/resend duplicates do not evict the flood-dedup entries.
- `src/helpers/StaticPoolPacketManager.*` – `peek()` / `peekNextOutbound()` (non-consuming) and reset of `sending_attempts` / `no_retry` / `final_hop_ack_resend` in `free()` (see below).
- `src/helpers/CommonCLI.*`, `docs/cli_commands.md`, `examples/*/MyMesh.*`, `NodePrefs.h` – config knobs and persistence.

## Note: pool-reuse fix

`sending_attempts` and `final_hop_ack_resend` were originally zeroed only in the `Packet` constructor, **not** when the packet was returned to the pool (`free()`). A recycled pool slot thus kept a stale `sending_attempts >= getMaxResendAttempts()`, causing `resendPacket()` to skip the resend for every subsequent DIRECT packet routed through that slot. Symptom on hardware: resends work right after boot (fresh pool) but degrade to zero once the pool has turned over (~`pool_size` sends) — forwards kept working because they do not check `sending_attempts`. `StaticPoolPacketManager::free()` now resets the cached hash and all resend fields in that chokepoint.

## Status & scope

- Rebased on `upstream/dev`; three commits: simulator hooks, the resend feature, and the TRACE resend fixes (found by @Kernel-Error, measured by @m0urs — see m0urs/MeshCore#25, m0urs/MeshCore#26).
- Applies exclusively to **DIRECT/TRACE-routed** traffic. Flood traffic is unaffected.
- Only active when `max.resend > 0` (default `2`).
