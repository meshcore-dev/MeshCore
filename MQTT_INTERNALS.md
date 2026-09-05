# MQTT Bridge Internals

Developer-facing notes on how the MQTT observer feature is structured in the codebase: source files, the seams that keep it isolated from upstream MeshCore code, and how on-device settings are migrated across firmware versions. For user-facing setup and CLI reference, see [MQTT_IMPLEMENTATION.md](MQTT_IMPLEMENTATION.md).

## Files

### Core Implementation
- `src/helpers/bridges/MQTTBridge.h` - MQTT bridge class definition
- `src/helpers/bridges/MQTTBridge.cpp` - MQTT bridge implementation
- `src/helpers/MQTTPresets.h` - Preset definitions, CA certificates, and lookup functions
- `src/helpers/MQTTDefaults.h` - Compile-time defaults for fresh `/mqtt_prefs`
- `src/helpers/MQTTMessageBuilder.h` - JSON message formatting utilities
- `src/helpers/MQTTMessageBuilder.cpp` - JSON message formatting implementation
- `src/helpers/JWTHelper.h` - JWT token generation for Ed25519-based authentication
- `src/helpers/CommonCLI_Observer.cpp` - All observer CLI command handling (MQTT, WiFi,
  timezone, NTP, OTA, SNMP, alerts)

### Integration seams with upstream code

The observer feature is kept out of upstream-tracked files through three mechanisms:

- **CLI hook methods** — upstream `CommonCLI.cpp` delegates to three `CommonCLI`
  methods defined in the fork-owned `CommonCLI_Observer.cpp`: `handleObserverCommand()`,
  `handleObserverSetCmd()`, and `handleObserverGetCmd()`. Each returns `true` if it
  consumed the command, otherwise the upstream parser runs. Only these three call
  sites touch upstream CLI code.
- **Callback virtuals** — observer behaviour needed from the application is exposed
  as default-no-op virtuals on `CommonCLICallbacks` (e.g. `restartBridgeSlot`,
  `isMqttBridgeRunning`, `syncMqttNtp`, `onAlertConfigChanged`, `sendAlertText`,
  `resolveAlertScope`, `beginDeferredOtaUpdate`). The example apps override them
  behind `#ifdef WITH_MQTT_BRIDGE`.
- **Separate settings file** — observer settings (MQTT slots, WiFi, timezone, SNMP,
  radio watchdog, fault alerts) live in the `MQTTPrefs` struct persisted to
  `/mqtt_prefs`, keeping `NodePrefs` / `/com_prefs` aligned with the upstream layout.

Remaining integration points in upstream files:
- `examples/simple_repeater/MyMesh.{h,cpp}`, `examples/simple_room_server/MyMesh.{h,cpp}` -
  bridge/alerter/SNMP wiring and packet-feed hooks, guarded by `#ifdef WITH_MQTT_BRIDGE`;
  plus the `createObserverPacketManager()` call in each constructor (see below)
- `src/helpers/CommonCLI.{h,cpp}` - the three CLI hooks, `MQTTPrefs` load/save/migration
- `src/Dispatcher.{h,cpp}` - radio watchdog block, guarded by `#ifdef WITH_MQTT_BRIDGE`

### Capture vs. duty-cycle throttling

RX processing needs a free packet from the static pool before `logRx()` (and thus the
MQTT uplink) can run — `Dispatcher::checkRecv()` silently discards received data when
the pool is empty. Because the outbound queue holds pool packets with no expiry,
duty-cycle throttling can park the entire pool waiting on TX budget, capping capture at
the TX rate — and the parked repeats absorb every budget refill, starving the node's
own CLI responses and making it un-administrable over the mesh. Observer builds
therefore use `RxReservePacketManager` (fork-owned,
`src/helpers/RxReservePacketManager.h`): below the RX reserve (a quarter of the pool)
it sheds only low-priority outbound (multi-hop flood repeats, adverts, trace), keeping
the node's own responses/ACKs queueable; below a smaller emergency floor it sheds
everything to keep capture alive. Queued packets still untransmitted 30 s past their
scheduled time are expired at dequeue, so under throttle the queue holds only fresh
traffic and admin responses reach the trickle of TX budget. Non-observer builds keep
the upstream pool behavior.

### Neighbors publication path (PSRAM only)

Periodic neighbors publishing is gated on `WITH_MQTT_NEIGHBORS`
(`defined(BOARD_HAS_PSRAM) && defined(MAX_NEIGHBOURS) && MAX_NEIGHBOURS > 0`,
defined in `MQTTBridge.h`). It spans two subsystems and two cores:

- **Mesh side (Core 1), `MyMesh`**: the `loop()` runs a two-stage refresh driven by
  `mqtt_neighbors_interval`. Stage 1 sends a zero-hop `sendNodeDiscoverReq()` and waits
  out its 60 s collection window to refresh `neighbours[]`. Stage 2
  (`startNeighborDiscover`) fires one anon-regions scope query per heard neighbor,
  overlaying them onto the peer-index space at `NEIGHBOR_DISCOVER_PEER_BASE` so their
  `PAYLOAD_TYPE_RESPONSE` packets decrypt via `searchPeersByHash`/`getPeerSharedSecret`/
  `onPeerDataRecv` even when the neighbor is not an ACL client. A reply is zero-hop by
  request, so `handleNeighborDiscoverResponse` also re-stamps `heard_timestamp` in both
  the snapshot and `neighbours[]` — proof of reception, and the only thing that heals a
  stamp taken before the clock was set. After all responses land or a 30 s window
  expires, `finishNeighborDiscover()` builds the JSON with
  `MQTTMessageBuilder::buildNeighborsMessage` into a transient buffer (PSRAM where
  available, internal DRAM otherwise) and hands it to the bridge. The entry table and its
  hex strings share one heap block sized to the pass — at `MAX_NEIGHBOURS` they reach
  ~4.5 KB, which does not fit the mesh loop task's 8 KB stack. Ages that still span a clock epoch publish as `null` rather than a
  fabricated delta (`neighborHeardAgeUsable`, see `UPSTREAM_BUGS.md` #1).
- **Buffer sizing**: `NEIGHBORS_JSON_BUFFER_SIZE` is 10 KB with PSRAM and 4 KB without,
  since a non-PSRAM board pays for the persistent buffer, the transient build buffer and
  the ArduinoJson pool out of the same internal DRAM each TLS slot needs ~40 KB of
  (~13 KB peak instead of ~35 KB). The pool has its own budget
  (`NEIGHBORS_DOC_POOL_BUDGET`) because ArduinoJson v7 hands out pool blocks in fixed
  4096-byte chunks, so a table that just fits the text buffer can still need well over
  it in pool — and a starved pool sets `doc.overflowed()`, which drops the entire publish
  instead of truncating it. `NEIGHBORS_MAX_PUBLISH_ENTRIES` (20 without PSRAM) keeps the
  pool inside a single block.
- **Bridge side, handoff**: `requestPublishNeighbors(json, len)` (Core 1) memcpys into the
  persistent buffer (`NEIGHBORS_JSON_BUFFER_SIZE`, PSRAM where available) and sets
  `_neighbors_publish_pending` with a release store; the MQTT task (`mqttTaskLoop`, Core 0)
  consumes it with an acquire load, calls `publishNeighbors()`, and clears the flag. A
  second snapshot is dropped while one is in flight. `publishNeighbors()` sends QoS 1,
  retain = `preset->allow_retain` (custom slots non-retained). MeshRank slots are included,
  publishing to `meshrank/uplink/{token}/{device}/neighbors` (non-retained, since the
  preset sets `allow_retain = false`).
- **Status reporting**: `MyMesh` reports the schedule each loop via
  `setNeighborsSchedule(phase, secs)`; `formatMqttStatusReply` renders it as the trailing
  `nbr: <when>/<last>` field in `get mqtt.status` while the feature is enabled.

The JSON builder lives in the pure, host-tested `MQTTPayloadBuilder`
(`test/test_mqtt_payload_builder`); the topic type in `MQTTTopicRouter`
(`test/test_mqtt_topic_router`). The mesh↔bridge orchestration above is on-target only.

### Runtime construction and slot memory

- **Deferred construction** — `MQTTBridge` is heap-allocated in each app's `begin()`
  (`bridge = new MQTTBridge(...)` in `MyMesh.cpp`) rather than held as a static member,
  because constructing it at static-init time crashes on ESP32 classic.
- **Runtime slot array** — `RUNTIME_MQTT_SLOTS` (`MQTTPresets.h`) is 6 with PSRAM and 3
  without, saving ~1.2 KB of heap on non-PSRAM boards. `MAX_MQTT_SLOTS` stays 6 on every
  build because it fixes the persisted `MQTTPrefs` layout, so slot config survives moving
  firmware between board classes. Three runtime slots suffice without PSRAM:
  `_max_active_slots` caps those boards at 2 live connections, leaving one spare for
  reconfiguration. Configured slots past the cap report `(inactive)`.
- **Buffers** — the 768-byte JWT `auth_token` is inline in every `MQTTSlot`, not allocated
  per JWT-auth slot. What varies is the MQTT client's TX/RX buffer: 896 bytes (the minimum
  that fits a CONNECT plus a 768-byte JWT) uniformly on PSRAM boards to limit
  fragmentation from mixed allocations, and 896 or 512 per slot on non-PSRAM boards so
  non-JWT slots leave smaller holes across teardown/recreate cycles. The large
  JSON/raw-packet buffers go through `psram_malloc()`, which prefers PSRAM and falls back
  to internal heap.

### Reconnection, backoff, and circuit breaker

The client's own auto-reconnect is disabled (`setAutoReconnect(false)`); the bridge drives
reconnection per slot.

- Backoff ladder: 10 s → 30 s → 60 s → 120 s → 300 s, staggered by 3 s × slot index so
  slots don't all handshake at once.
- The ladder resets only after a connection has held for 2 minutes
  (`BACKOFF_STABLE_RESET_MS`), which is longer than the 75 s keepalive — a link that can't
  survive one keepalive round-trip keeps its earned rung instead of hammering TLS
  handshakes at the 10 s rung. CONNACK alone does not reset it.
- After 3 more failures at the top rung (~15 min) the slot's circuit breaker trips and
  routine reconnects stop. A tripped slot is probed once every 30 minutes (with a fresh
  JWT where applicable); a successful connect clears the breaker, as does reconfiguring
  the slot.
- Message retransmit timeout is 15 s — one retry inside esp-mqtt's 30 s outbox expiry,
  preserving at-least-once delivery while capping duplicates at one.

### Message building

- The `hash` field in `packets` messages is MeshCore's own packet hash,
  `Packet::calculatePacketHash()` — SHA256 over the payload type and payload (plus
  `path_len` for TRACE), truncated to `MAX_HASH_SIZE`. It is the same value the dispatcher
  uses, so uplinked hashes match the mesh.
- `score` is recomputed at publish time from the packet's SNR and length via the radio's
  `packetScore()`, so it matches the value the firmware used on receive.
- Timezone: the JChristensen/Timezone object (`_timezone_storage`, inline since the
  memory-defrag work) is kept current from `timezone_string` via `setRules()`, but
  `formatIsoTimestampForMqtt()` explicitly ignores it — every published timestamp, time,
  and date field is UTC off `gmtime()`, matching Python's
  `datetime.now(timezone.utc).isoformat()`. The timezone prefs therefore do not affect
  MQTT message content.

### Command namespacing

CLI commands sit at two levels. `bridge.*` is low-level and shared by all bridge types
(MQTT, RS232, ESP-NOW): `bridge.enabled` is the master switch, and `bridge.source` selects
which packet events non-MQTT bridges capture. The MQTT bridge ignores `bridge.source` in
favour of independent `mqtt.rx` / `mqtt.tx` controls. Everything MQTT-specific lives under
`mqtt.*` (shared settings), `mqttN.*` (per-slot broker config), `wifi.*`, and `timezone.*`.

### `/mqtt_prefs` file format

`/mqtt_prefs` is written with an 8-byte `MQTTPrefsHeader` (`magic`, `version`,
`payload_len`) followed by the raw `MQTTPrefs` payload. The magic is
`{0xF5, 'M', 'Q', 'P'}` — its leading non-ASCII byte can never collide with the first
bytes of a legacy (headerless) file, whose payload begins with the `mqtt_origin`
string. Bump `MQTT_PREFS_VERSION` when the payload layout changes incompatibly; a file
whose version this firmware doesn't recognize is left untouched and the in-memory prefs
fall back to defaults (no downgrade, no misread). `saveMQTTPrefs()` also refuses to
write while such a file is present (`_mqtt_prefs_hold`), so a `set` command after a
firmware downgrade can't clobber the newer config — observer settings changed in that
state simply don't persist. The frozen legacy layouts are pinned with `static_assert`s
in `MQTTPrefsStorage.h`, so every target build re-verifies the fleet's file offsets.

Adding a field to the current version stays backward compatible: append it to the end
of `MQTTPrefs`, give the older exact payload length an explicit decoder boundary, and
leave the missing tail at its default. The packet-filter addition follows that rule:
the prior 2864-byte v1 payload loads with all six filters set to `all`, while the
full payload is 2876 bytes.

#### The downgrade contract

**Within a version tag the layout is append-only, and a longer payload is always
readable.** A file written by a later build starts with this binary's exact baseline,
so `classify()` reads that prefix and ignores the tail. A downgraded node keeps its
WiFi credentials, broker slots, and every other setting it understands; the only thing
it loses is the settings the newer build added.

That asymmetry is the whole point. Refusing the file costs the operator the network
itself — `/mqtt_prefs` holds `wifi_ssid`/`wifi_password` as well as the broker config,
so a node that falls back to defaults has no WiFi, no portal, and no OTA, recoverable
only over serial. Reading it costs a feature's settings. Losing later settings is the
acceptable half of that trade; losing the node is not.

The tail survives until something actually writes. `saveMQTTPrefs()` rewrites at this
binary's own length, so a rollback that changes no observer setting and is later rolled
forward keeps the newer fields intact — only an explicit `set` while downgraded drops
them. The boot log says so when it happens.

**A change that is not a pure append MUST bump `MQTT_PREFS_VERSION`.** The version
check is what makes the rule above safe: a different tag is still refused outright and
the file preserved, because the bytes may no longer mean what this binary thinks. Never
reorder, resize, or repurpose an existing field within a version.

Note the rule is only as old as the build that implements it. Firmware already deployed
carries the *previous* decoder, which rejects any longer v1 payload — so rolling back
from this build to one shipped before it still falls back to defaults.
`MQTTPrefsCodec::payloadLenFor()` covers that gap from the writing side: it returns the
shortest length that still round-trips the configuration, so a node keeps writing 2864
bytes until a packet filter actually holds something, and clearing the last non-default
filter puts it back. That mitigation can be retired once no supported downgrade target
predates the contract; the contract itself is the durable half.

Shorter payloads keep their existing, stricter treatment: a short length must match a
boundary that really shipped (`MQTT_PREFS_V1_*_PAYLOAD_SIZE`), because raw prefs have
no checksum and an arbitrary short size cannot be trusted to mean anything.

### Settings upgrade / migration

`loadPrefs()` handles every historical on-device format one-time at boot:
- **`/mqtt_prefs`** — if the file has the version header it is read directly. Otherwise
  it is a legacy headerless file and its layout is detected by size: pre-slot
  (`OldMQTTPrefs`), 3-slot (`ThreeSlotMQTTPrefs`), or the 6-slot layout shipped on
  `observer-firmware` back when it was named `mqtt-bridge-implementation-flex`
  (`Legacy6SlotMQTTPrefs`). Each is field-copied into
  the current compact `MQTTPrefs` and re-saved with the version header — which also
  drops the vestigial `_legacy_*` fields the flex layout carried mid-struct. This is a
  one-time rewrite; every deployed device performs it on its first boot of versioned
  firmware, after which all reads take the header path.
  The pre-slot (`OldMQTTPrefs`) copy maps the old single-broker keys onto slots:
  `mqtt.analyzer.us = on` → slot 1 `analyzer-us`, `mqtt.analyzer.eu = on` → slot 2
  `analyzer-eu`, and a configured `mqtt.server` / `mqtt.port` / `mqtt.username` /
  `mqtt.password` → slot 3 `custom` with those values preserved. Origin, IATA, message
  types, WiFi, and timezone carry over as-is.
- **`/com_prefs`** — a file written by fork firmware that predates the `MQTTPrefs` split
  (a zero-filled MQTT gap plus a trailing observer block) is detected by size; the
  trailing SNMP / radio-watchdog / fault-alert settings and the `rx_boosted_gain` /
  `flood_max_*` fields are recovered, carried into `/mqtt_prefs`, and both files are
  rewritten in the current formats.
- Settings the pre-split firmware stored *inside* the `/com_prefs` MQTT gap (the MQTT
  slot/WiFi config itself) are **not** recovered — users upgrading from firmware that
  old must re-enter their MQTT and WiFi configuration.
