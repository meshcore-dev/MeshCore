# Bugfix: noise-floor median estimator (branch `fix/noise-floor-ratchet`)

Branched off `dev` at `102f1d2a`. Fix commit: `d9f68e9e`.

## Symptom

On long-running nodes (ufo integration branch, which combines
`feature/repeated-sending-2` + `feature/quiet-dwell`), direct-packet resends
and dwell-gated TX get deferred or suppressed even on a quiet channel. The
radio's reported `noise_floor` drifts to the `-120` clamp and never recovers,
so the RSSI-margin LBT checks (`isResendChannelActive`, `isChannelNoisy`,
`isChannelActive` with `interference_threshold`) stay permanently
over-sensitive — every send looks like it collides with noise.

This is a **secondary** cause of "resends degrade to zero after long uptime."
The primary cause (stale `sending_attempts` on pool reuse) is fixed separately
on `feature/repeated-sending-2` (commit `110b9b40`, in `free()`).

## Root cause

`RadioLibWrapper::loop()` calibrated `_noise_floor` from a 64-sample block, but
only accepted samples that satisfied

```
rssi < _noise_floor + SAMPLING_THRESHOLD   // SAMPLING_THRESHOLD = 14
```

That filter is a **one-way ratchet**: it admits ever-lower samples but rejects
anything above the current floor, so the block mean can only move down. Over
time it walks to the `-120` lower clamp and sticks there. The only thing that
reset it was `resetAGC` setting `_noise_floor = 0` — but `resetAGC` is gated on
`agc_reset_interval`, which defaults to `0` (off), and forcing `0` would
anyway open a brief permissive LBT window (`margin = RSSI − 0`) until the next
block completes.

## The fix

Replace the ratcheted block mean with the **median** of the 64-sample block:

- Accept **every** idle sample (`!isReceivingPacket()`) — no downward bias.
- Sort the 64 samples and take the median (mean of the two middle values).
  The median rejects transient interference spikes in **both** directions and
  recovers upward as well as down.
- Write `_noise_floor` **only after a full block** is collected. The previous
  value stays valid while the next block is sampled — no reset-to-0, hence no
  permissive LBT window during reconvergence.
- Clamp the result to `-120` (lower bound of the radio's RSSI range).
- `resetAGC()` no longer touches `_noise_floor`; it only discards the
  in-progress block (the analog frontend was just reset, so queued samples are
  stale). `_noise_floor` itself is left in place because the median estimator
  no longer needs the hard reset that the ratchet did.

`SAMPLING_THRESHOLD` is removed (it only fed the ratchet filter).
`NUM_NOISE_FLOOR_SAMPLES` (64) moves to the header so the sample buffer can be
a member array.

## Files

| File | Change |
|---|---|
| `src/helpers/radiolib/RadioLibWrappers.h` | `NUM_NOISE_FLOOR_SAMPLES` macro; replace `_floor_sample_sum` with `_floor_samples[64]` + `_floor_block_ready` flag |
| `src/helpers/radiolib/RadioLibWrappers.cpp` | `sortInt16()` helper; `loop()` median logic; `resetAGC()` no longer zeros `_noise_floor`; `triggerNoiseFloorCalibrate()`/`begin()` reset the block state |

## Build verification

The sim build (`cargo check -p mcsim-firmware`) does **not** compile
`RadioLibWrappers.cpp` — it links a `sim_radio.cpp` mock. Verified with a real
firmware target instead:

```
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware Heltec_v3_repeater
# -> SUCCESS (14.7s), RadioLibWrappers.cpp compiles against real RadioLib
```

## Merge note

This branch is `dev`-based and touches only the noise-floor calibration. It is
meant to be merged into `ufo` alongside `feature/repeated-sending-2`; on plain
`dev`, `_noise_floor` is consumed only when `interference_threshold != 0`, so
the bug bites primarily on `ufo` where `quiet-dwell` and `repeated-sending-2`
read it unconditionally.
