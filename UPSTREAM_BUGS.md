# Upstream Bug Candidates

Bugs in code this fork inherits from upstream (`meshcore-dev/dev`), each with the
fix we would propose upstream. Any fork-local workaround is recorded so it can be
dropped once upstream lands a real fix.

Status: **open** (not reported) · **reported** (issue/PR filed) · **landed**
(upstream fixed, fork workaround removable).

---

## 1. Neighbour "heard" ages are derived from a steppable wall clock

**Status:** open
**Files:** `examples/simple_repeater/MyMesh.cpp`, `examples/simple_room_server/MyMesh.cpp`
**Impact:** wrong ages reported to companion apps, CLI, and (in this fork) MQTT. No crash.

### Symptom

A repeater published a neighbour age of 69681995 seconds — 806 days — for two of
three neighbours that had just answered a live query (capture abridged; see
`MQTT_IMPLEMENTATION.md` for the full payload shape):

```json
{"timestamp":"2026-07-30T22:59:09.000000+00:00","total_neighbors":3,
 "neighbors":[
   {"pubkey":"0CE5EA7C…","snr":12.5,"heard_secs_ago":57,"status":"responded"},
   {"pubkey":"B0D17C59…","snr":12.5,"heard_secs_ago":69681995,"status":"responded"},
   {"pubkey":"D19BEE16…","snr":12.25,"heard_secs_ago":69681995,"status":"responded"}]}
```

### Root cause

`heard_secs_ago` is a *relative* quantity computed by subtracting two samples of
an *absolute* wall clock that can step by years mid-session.

`putNeighbour()` stamps `heard_timestamp = getRTCClock()->getCurrentTime()` when a
zero-hop advert or node-discover reply arrives (`MyMesh.cpp:93`). On a cold boot
the radio is receiving within a second or two, while the clock is still at the
firmware's unset default — `1715770351`, 15 May 2024 (`src/helpers/ArduinoHelpers.h`,
`src/helpers/ESP32Board.h`). NTP (or an admin over the CLI) then steps the clock
forward by years, and every later reader subtracts the two:

```
publish epoch          1785452349   (2026-07-30T22:59:09Z)
− reported age           69681995
= stored heard_timestamp 1715770354   = unset-clock default + 3 s of uptime
```

So both stale entries were stamped 3 seconds after boot. The entries do not
self-heal: `putNeighbour()` is the only writer of `heard_timestamp`, so an entry
stays wrong until that specific neighbour is physically heard again. In the capture
above the two stale neighbours missed the node-discover refresh (responder-side
rate limit `discover_limiter`, a reply collision, or firmware without
`CTL_TYPE_NODE_DISCOVER_RESP`) yet still answered a direct query — hence
`"status":"responded"` next to a 2-year age.

Deferring the *read* until after time sync does not fix this. The bad value is
written at packet-reception time, which on any cold boot precedes WiFi and NTP.

### Affected readers

| Path | Location |
| --- | --- |
| Companion `REQ_TYPE_GET_NEIGHBOURS` binary reply | `MyMesh.cpp:404` |
| CLI `neighbors` text output | `MyMesh.cpp:1350` |
| MQTT neighbors JSON (fork-only) | `MyMesh.cpp:2036` |

### Proposed upstream fix

Stamp neighbours from a monotonic source and derive every age from that:

- add `uint32_t heard_millis` to `NeighbourInfo`, set from `millis()` in
  `putNeighbour()` alongside the existing wall-clock stamp;
- compute every "seconds ago" readout as `(now_millis - heard_millis) / 1000`
  using the unsigned-subtraction idiom already used by `millisHasNowPassed()` /
  `futureMillis()`, which is immune to clock steps and stays correct across one
  32-bit rollover;
- clamp the result to a documented maximum so an entry untouched for longer than
  the 49.7-day `millis()` period cannot alias back to a small age;
- keep `heard_timestamp` for anything that needs an absolute instant — sort order
  is equivalent under either field.

Cost: 4 bytes × `MAX_NEIGHBOURS` of RAM.

### Fork-local containment (in place)

Not a substitute for the above; it stops the fork from publishing a fabricated
number while upstream still stamps from the wall clock.

- `finishNeighborDiscover()` reports the age as unknown — `"heard_secs_ago":null`
  — when the stored stamp predates `MQTTConnectionPolicy::kSyncedClockEpoch`
  (2025-01-01, above the unset-clock default) while the current clock is past it,
  or when the clock has stepped backwards. Previously a backwards clock reported
  `0`, i.e. "heard just now".
- `handleNeighborDiscoverResponse()` re-stamps `heard_timestamp` on a zero-hop
  scope reply, in both the snapshot and the live table. The reply is proof of
  reception, so entries now heal once per discovery cycle instead of waiting for
  the neighbour's next advert. This would have made 2 of the 3 entries above
  correct.
- Publish ordering places known ages before unknown ones, so an unusable stamp
  cannot masquerade as the freshest entry when the JSON buffer truncates.

---

## 2. Neighbour age subtraction is unclamped in the companion and CLI readouts

**Status:** open
**Files:** `examples/simple_repeater/MyMesh.cpp:404`, `:1350` (and the room-server twins)
**Impact:** age underflows to ~4.29e9 instead of a sane value.

Both readers subtract without checking ordering:

```cpp
uint32_t heard_seconds_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
```

Any state where `heard_timestamp > now` — clock stepped backwards by NTP or an
admin, or a stamp taken before a backwards step — wraps the unsigned result to
near `UINT32_MAX`. Distinct from #1 in that it is a missing guard rather than the
choice of clock: worth fixing on its own even before the monotonic change, by
clamping to `0` (or reporting "unknown", which is what the age actually is).
