# MeshCore — Friday Context

## What This Is
Our fork of MeshCore firmware. Goal: improve routing efficiency, add compression, port useful Meshtastic features, and build an iterative testing harness for algorithmic improvements.

**Upstream:** https://github.com/meshcore-dev/MeshCore.git (remote: `upstream`)
**Reference:** `/Users/ryan/Documents/Development/meshtastic-reference` — Meshtastic firmware, read-only reference

## Ticketmaster ID
`meshcore`

## Stack
- **Language:** C/C++ (Arduino framework via PlatformIO)
- **Build:** PlatformIO (`platformio.ini` + `variants/*/platformio.ini`)
- **Radio:** RadioLib ^7.6.0
- **Crypto:** Ed25519, AES-128, HMAC-SHA256 (rweather/Crypto)
- **Targets:** ESP32, nRF52, RP2040, STM32

## Architecture — Key Files
| File | Role |
|------|------|
| `src/Mesh.cpp` | Core routing engine — flood, direct, path learning |
| `src/Mesh.h` | Virtual extension points for all custom behavior |
| `src/Packet.h/cpp` | Wire format, header bit-packing, path encoding |
| `src/Dispatcher.h/cpp` | Radio I/O, tx budget, duty cycle, CAD |
| `src/Utils.h/cpp` | AES-128, HMAC-SHA256, key derivation |
| `src/Identity.h/cpp` | Ed25519 signing/verification |
| `src/helpers/BaseChatMesh.*` | Higher-level messaging, contacts, keep-alive |
| `src/helpers/CommonCLI.*` | Serial CLI — 50+ commands |

## Extension Points (Virtual Methods in Mesh.h)
- `filterRecvFloodPacket()` — drop/modify inbound floods
- `allowPacketForward()` — control repeating
- `getRetransmitDelay()` — tune backoff
- `onPeerDataRecv/onAdvertRecv/onPathRecv/...` — packet handlers
- `logRx/logTx` — telemetry hooks

## Improvement Roadmap
1. **Compression engine** — port Unishox2 from Meshtastic; hook into payload encode/decode path
2. **Routing algorithm variants** — SNR-weighted relay selection, neighbor table scoring
3. **MQTT bridge** — port from Meshtastic; internet gateway capability
4. **Store & Forward** — port from Meshtastic; offline message persistence
5. **Test harness** — simulated multi-node environment for iterative benchmarking
6. **Telemetry/metrics** — per-packet SNR, RSSI, airtime, delivery latency logging

## Rules
- Never push to `upstream` — it's read-only reference
- All new features go in `src/helpers/` or as new modules under `src/`
- Prefer extending via virtual methods over modifying core `Mesh.cpp`
- Keep Meshtastic port code clearly tagged with `// PORT: meshtastic`
- Run `pio run` on at least one ESP32 target before committing
