# Repeated Sending (branch `feature/repeated-sending-2`)

Reliable delivery of direct-routed packets via **cancellable retransmissions**. The feature augments DIRECT/TRACE routing with an automatic, self-limiting retransmission that switches itself off one hop downstream as soon as successful forwarding is detected.

## Problem

A direct-routed packet (DIRECT / TRACE) that travels to its destination via one or more relays is considered "sent" by the originator as soon as it is on air. If it is lost on the first hop (collision, interference, fading), there is no retry at the MeshCore layer — the packet is gone, and the application layer must re-schedule the entire message. Flood traffic does not have this problem because it is widely broadcast anyway; DIRECT packets, however, are the cost-conscious path for targeted messages.

## Solution

After every DIRECT packet is sent, the Dispatcher schedules a retransmission. The key trick: **the originator listens for a downstream relay forwarding the packet**. As soon as it receives that forward (the "forwarding echo"), it knows its packet survived at least the next hop and cancels the pending retransmission. The result is an error-correction mechanism that produces **no** extra packet in the success case, and in the failure case retransmits precisely where the packet actually got stuck.

Flow in detail:

1. **Schedule the resend** – `Dispatcher::resendPacket()` runs after TX only for DIRECT packets with at least one relay hash in the path (`getPathHashCount() > 0`). It does not wait for a timeout; instead it re-queues the packet after a computed **silence period** (packet airtime × budget factor + jitter + linear backoff per attempt). The delay gives the downstream repeater time to forward — and gives the originator time to hear that forward *before* it retransmits itself.
2. **Detect the forward and cancel** – In `Mesh::onRecvPacket()`, every received DIRECT packet is checked: is it a downstream forward of one of our own pending resends (`Packet::isRetryMatch()`)? If so, the resend is removed from the outbound queue (`removeOutboundByIdx`). The RX side drains the entire FIFO in a single `loop()` pass to keep the window between forwarding echo and scheduled resend as small as possible.
3. **Non-invasive LBT** – Resends use *no* hardware CAD, but `isResendChannelActive()`: a pure IRQ/register read (preamble detection + RSSI margin above the noise floor). RX stays open so the forwarding echo is not missed. First sends still use normal CAD (collision avoidance is worth the momentary deafness).

### Final-hop handling

On the final relay hop there is no downstream forward to overhear — the destination does not forward. Two special paths prevent the last hop from being left unprotected, or the destination from being flooded:

- **TXT_MSG with ACK** – the destination acknowledges receipt with an ACK. This hop is allowed exactly one ACK-cancellable resend (flag `final_hop_ack_resend`). `Mesh::cancelPendingFinalHopResend()` cancels the oldest final-hop resend still sitting in the queue as soon as the ACK returns (FIFO, since ACKs are produced in delivery order). A resend already on air is deliberately not aborted (the destination dedups the duplicate via `wasSeen`).
- **TRACE** – TRACE packets append an SNR byte per hop, which changes their hash. `isRetryMatch()` therefore compares payload + SNR-path prefix instead of the hash for TRACE. At the final forwarding hop the retry budget is exhausted directly (`sending_attempts = getMaxResendAttempts()`), since a non-cancellable resend here would only burden the destination.

### Considerate noise-floor calibration

`Dispatcher::loop()` moves noise-floor calibration to the end of `loop()` and only runs it when the radio is demonstrably idle (`!isReceiving()`) **and** the outbound queue is empty (`getOutboundCount() == 0`). Calibration briefly takes the radio out of RX — that would disrupt pending, time-critical resends or abort a packet not yet read out.

## Configuration

| Location | Value | Meaning |
|---|---|---|
| CLI `set max.resend <0–3>` / `get max.resend` | Default `2` | Maximum resend attempts for DIRECT packets. `0` disables the feature entirely. |
| `NodePrefs` (persisted) | Byte offset `295` | Loaded/saved to file via `CommonCLI`; sanitised to 0–3. |
| Companion-radio `CMD_SET_*` frame | Byte 6 in the path block | App-protocol path to set `max_resend_attempts` from the companion. |
| `RESEND_INTERFERENCE_MARGIN` (compile-time) | Default `12` dB | Margin above the noise floor at which a resend is blocked (non-invasive LBT). |
| `RESEND_BACKOFF_STRETCH_MS` (compile-time) | Default `1000` ms | Linear backoff per resend attempt, to ride out longer interference bursts. |

## Implementation (core files)

- `src/Dispatcher.cpp` / `.h` – `resendPacket()`, `isResendChannelActive()`, `getMaxResendAttempts()`, relocated noise-floor calibration, RX draining.
- `src/Mesh.cpp` / `.h` – forwarding detection and resend cancellation in `onRecvPacket()`, `cancelPendingFinalHopResend()`, final-hop marking.
- `src/Packet.cpp` / `.h` – cached packet hash (`hash`, `hash_hex`), `isRetryMatch()` (TRACE-specific vs. hash-based), fields `sending_attempts` / `final_hop_ack_resend`.
- `src/helpers/SimpleMeshTables.h` – dedicated ACK dedup table (`_acks[]`, via `ack_crc`), so multi-ACK/resend duplicates do not evict the flood-dedup entries.
- `src/helpers/StaticPoolPacketManager.*` – `peek()` / `peekNextOutbound()` (non-consuming) and reset of `sending_attempts` / `final_hop_ack_resend` in `free()` (see below).
- `src/helpers/CommonCLI.*`, `docs/cli_commands.md`, `examples/*/MyMesh.*`, `NodePrefs.h` – config knobs and persistence.

## Note: pool-reuse fix

`sending_attempts` and `final_hop_ack_resend` were originally zeroed only in the `Packet` constructor, **not** when the packet was returned to the pool (`free()`). A recycled pool slot thus kept a stale `sending_attempts >= getMaxResendAttempts()`, causing `resendPacket()` to skip the resend for every subsequent DIRECT packet routed through that slot. Symptom on hardware: resends work right after boot (fresh pool) but degrade to zero once the pool has turned over (~`pool_size` sends) — forwards kept working because they do not check `sending_attempts`. Commit `110b9b40` resets both fields in the chokepoint `StaticPoolPacketManager::free()`.

## Status & scope

- Three feature commits on top of a merge of `upstream/dev`: `84bc3faf` (final-hop ACK), `2c48f09c` (non-invasive resend LBT + backoff), `110b9b40` (pool-reuse fix).
- Applies exclusively to **DIRECT/TRACE-routed** traffic. Flood traffic is unaffected.
- Only active when `max.resend > 0` (default `2`).
