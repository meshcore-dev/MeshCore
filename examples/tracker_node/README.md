# tracker_node — Kestrel PLI Beacon Firmware

Standalone MeshCore firmware that broadcasts Position Location Information (PLI)
over the mesh every 30 seconds (configurable). PLI packets are encrypted group-data
datagrams (`PAYLOAD_TYPE_GRP_DATA`, data type `0x0E51`) compatible with the Kestrel
iOS/macOS TAK client.

## What it does

- Reads GPS from the on-board GNSS receiver (enabled automatically)
- Packs lat/lon/alt/battery/role/callsign into a `KestrelPLIPacket`
- Broadcasts it as an authenticated group-data flood on the configured squad channel
- Blinks the status LED on every transmission
- Exposes a simple serial CLI for configuration

## Supported hardware

| Board | Env name | Notes |
|-------|----------|-------|
| Heltec Wireless Tracker V2 | `heltec_tracker_v2_tracker` | ESP32-S3, SX1262, GPS, TFT display. Primary dev target. |
| Seeed SenseCAP T1000-E | `t1000e_tracker` | nRF52840, LR1110, GPS. Production CASEVAC kit target. |

## Build

```
# from MeshCore repo root
pio run -e heltec_tracker_v2_tracker -d examples/tracker_node
pio run -e t1000e_tracker            -d examples/tracker_node
```

## CLI commands (Serial, 115200 baud)

| Command | Description |
|---------|-------------|
| `set_callsign <name>` | Set callsign (1-16 chars), persisted |
| `set_interval <secs>` | Set broadcast interval (10-300 s), persisted |
| `set_role <0-3>` | Set role: 0=team member, 1=team lead, 2=casevac, 3=asset |
| `emergency` | Switch to 5 s interval + broadcast immediately |
| `normal` | Return to configured interval |
| `status` | Print GPS fix, callsign, interval, battery |
| `broadcast` | Force an immediate PLI transmission |

## How it appears in Kestrel

- Each tracker appears as a PLI dot on the map, updated every broadcast interval
- Role 2 (`casevac`) renders with a distinct CASEVAC marker (red cross overlay)
- Role 1 (`lead`) shows a star marker
- Dot colour reflects battery level (green → amber → red)
- Tapping the dot shows callsign, last seen time, and altitude

## Packet format

Data type `0x0E51` (`KESTREL_PLI_DATA_TYPE_U16`) inside a `PAYLOAD_TYPE_GRP_DATA`
group datagram, encrypted with the squad channel key. See
`src/helpers/KestrelPLI.h` for the full packed struct definition.
