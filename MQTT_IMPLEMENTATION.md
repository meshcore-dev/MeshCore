# MQTT Bridge Implementation for MeshCore

This document describes the MQTT bridge implementation that allows MeshCore repeaters to uplink packet data to multiple MQTT brokers.

## Quick Start Guide

**Prerequisites:** a MeshCore device flashed with observer MQTT firmware, WiFi network
credentials, and console access — either serial (115200 baud) or repeater login via the
companion app.

### Essential Commands to Get MQTT Observer Running

**1. Flash the observer firmware to your device**

The easiest route is the [MeshCore Observer Flasher](https://observer.gessaman.com/) — pick
**MQTT Observer Firmware**, select your device, and flash from the browser (Chrome or Edge).
To build it yourself instead, use one of the observer build targets (e.g.
`heltec_v4_repeater_observer_mqtt`) — see [Build Configuration](#build-configuration).

After flashing, connect to the device console via serial (115200 baud) or repeater login.

**2. Configure radio settings**

If this is a fresh flash or full erase, configure your radio parameters first. These must match other nodes in your mesh:

```bash
set radio 910.525,62.5,7,5
set tx 22
```

Format: `set radio <freq_MHz>,<bw_kHz>,<sf>,<cr>`

**3. Configure device identity**

```bash
set name MyObserver
set mqtt.iata SEA
```

If migrating from an existing node (e.g., a Raspberry Pi gateway), restore the private key to keep the same identity:
```bash
set prv.key <your_64_hex_char_private_key>
```

**4. Configure WiFi credentials** (value is the rest of the line; do not use quotes — see [WiFi Commands](#wifi-commands))
```bash
set wifi.ssid YourWiFiNetwork
set wifi.pwd YourWiFiPassword
```

**5. (Optional) Choose which brokers to publish to**

Slots 1 and 2 default to Let's Mesh Analyzer US and EU. To add or change a broker, pick a
name from [Broker Presets](#broker-presets):
```bash
set mqtt3.preset meshmapper
```

**6. (Optional) Configure timezone**

```bash
set timezone America/New_York
```

Or use a plain offset as a fallback: `set timezone.offset -5`. Published timestamps are
always UTC either way — see [Timezone Commands](#timezone-commands).

**7. (Optional) Disable packet repeating**

If this observer is receive-only (e.g., using a PCB antenna in a location where repeating would be harmful), disable forwarding:
```bash
set repeat off
```

**8. Reboot to connect**
```bash
reboot
```

**9. Verify configuration**
```bash
get wifi.ssid
get wifi.status
get bridge.enabled
get mqtt.rx
get mqtt.tx
get mqtt.origin
get mqtt.iata
get mqtt1.preset
get mqtt2.preset
get mqtt3.preset
get mqtt.status
```

**That's it!** The device will now:
- Connect to WiFi automatically
- Start uplinking mesh packets to configured MQTT brokers
- By default, publish to Let's Mesh Analyzer US (slot 1) and EU (slot 2)
- Use device name as MQTT origin (set automatically)

---

## Overview

The MQTT bridge implementation provides:
- Up to 6 MQTT connection slots, each holding a built-in preset for a community broker or a custom broker of your own — see [Broker Presets](#broker-presets)
- Per-preset authentication over WSS, MQTT/TLS, or plain MQTT: Ed25519-signed JWT, fixed or per-slot username/password, or none — see [Authentication](#authentication)
- Automatic reconnection with exponential backoff
- JSON message formatting for status, packet, raw, and neighbors data
- Packet queuing during connection issues
- Automatic migration from old configuration format

## Broker Presets

Each of the 6 slots holds one preset. Point a slot at a community broker with:

```bash
set mqtt3.preset meshmapper    # slot 3 → MeshMapper
```

Most presets need nothing else — the broker address, transport, and credentials all ship
in the firmware. The **Extra setup** column below lists the exceptions. Presets using the
`meshcore/{iata}/…` topic layout (every built-in except `meshrank`) also need `set mqtt.iata`.

Run `get mqtt.presets` on the device for the list this firmware actually ships; the table
below documents the current build.

| Preset | Broker | Auth | Extra setup |
|--------|--------|------|-------------|
| `analyzer-us` | `wss://mqtt-us-v1.letsmesh.net:443/mqtt` | JWT | — (default slot 1) |
| `analyzer-eu` | `wss://mqtt-eu-v1.letsmesh.net:443/mqtt` | JWT | — (default slot 2) |
| `nz-analyzer` | `wss://meshcore-mqtt-1.baird.io:443` | JWT | — |
| `meshmapper` | `wss://mqtt.meshmapper.net:443/mqtt` | JWT | — |
| `meshrank` | `mqtts://meshrank.net:8883` | None (token in topic) | `set mqttN.token <token>` |
| `waev` | `wss://mqtt.waev.app:443/mqtt` | JWT | — |
| `meshomatic` | `wss://us-east.meshomatic.net:443/mqtt` | JWT | — |
| `cascadiamesh` | `wss://mqtt-v1.cascadiamesh.org:443/mqtt` | JWT | — |
| `tennmesh` | `mqtt://mqtt.tennmesh.com:1883` | User/pass (in firmware) | — |
| `nashmesh` | `mqtt://mqtt.nashme.sh:1883` | User/pass (in firmware) | — |
| `ctmesh` | `mqtt://mqtt.ctmesh.org:1883` | User/pass (in firmware) | — |
| `chimesh` | `wss://mqtt.chimesh.org:443` | JWT | — |
| `meshat.se` | `wss://meshcore-mqtt.meshat.se:443` | JWT | — |
| `eastidahomesh` | `mqtt://live.eastidahomesh.com:1883` | None | — |
| `coloradomesh` | `wss://mqtt.meshcore.coloradomesh.org:443` | JWT | — |
| `dutchmeshcore-1` | `wss://collector1.dutchmeshcore.nl:443/mqtt` | JWT | — |
| `dutchmeshcore-2` | `wss://collector2.dutchmeshcore.nl:443/mqtt` | JWT | — |
| `meshcore-ca-1` | `wss://mqtt1.meshcore.ca:443/mqtt` | JWT | — |
| `meshcore-ca-2` | `wss://mqtt2.meshcore.ca:443/mqtt` | JWT | — |
| `meshcore-fi` | `wss://mc-mqtt.meshcore.fi:443/` | JWT | — |
| `okimesh-1` | `wss://mqtt1.okimesh.org:9002/mqtt` | JWT | — |
| `okimesh-2` | `wss://mqtt2.okimesh.org:9002/mqtt` | JWT | — |
| `inwmesh` | `mqtts://scope.inwmesh.org:8883` | User/pass (per slot) | `set mqttN.username` + `set mqttN.password` |
| `bostonmesh` | `wss://mqttmc01.bostonme.sh:443/mqtt` | JWT | — |
| `rflab` | `wss://mqtt.rflab.io:443` | JWT | — |
| `ipnt.uk` | `wss://mqtt.ipnt.uk:443` | JWT | — |
| `flmesh` | `wss://mcmqtt.jntconnections.com:443` | JWT | — |
| `corecomms` | `wss://mqtt.corecomms.net:443/mqtt` | JWT | — |
| `meshtexas` | `wss://mqtt.meshtexas.org:443/mqtt` | JWT | — |
| `mesh-chaun14` | `mqtt://mqtt.mesh.chaun14.fr:1884` | User/pass (username is the device public key) | `set mqttN.password` |
| `wcmesh` | `wss://mqtt.wcmesh.com:443` | JWT | — |
| `atvirastinklas` | `wss://mqtt-mc.atvirastinklas.lt:443` | JWT | — |
| `gomesh` | `wss://mqtt.gomesh.dev:443` | JWT | — |
| `idahomesh` | `wss://mqtt.idahomesh.org:443/mqtt` | JWT | — |
| `ntxmesh` | `wss://ntxmesh.dhovin.me:8883` | JWT | — |
| `custom` | your own broker | User/pass, or JWT when `mqttN.audience` is set | `set mqttN.server` (see [custom broker setup](#custom-brokers)) |
| `none` | (slot disabled) | — | — |

Transport is the URL scheme: `wss://` is WebSocket Secure, `mqtts://` is MQTT over TLS,
and `mqtt://` is plain unencrypted MQTT. The two TLS schemes are what count against the
non-PSRAM slot limit below.

### Slots and Memory Limits

Fresh installs default to slot 1 `analyzer-us`, slot 2 `analyzer-eu`, and slots 3–6 `none`.

- **With PSRAM:** all 6 slots can be active simultaneously
- **Without PSRAM:** maximum 2 active TLS/WSS slots (each WSS/TLS connection requires ~40KB internal heap)
- Slots configured beyond what the device supports show as `(inactive)` in `get mqtt.status`
- Slot configuration is preserved in preferences — moving the firmware to a PSRAM device activates the rest

**Neighbors publication without PSRAM.** PSRAM boards get `WITH_MQTT_NEIGHBORS`
automatically. Non-PSRAM boards opt in per variant with
`-D MQTT_NEIGHBORS_WITHOUT_PSRAM=1`, which is set on the ESP32-S3 observer envs
(Heltec V3/WSL3, RAK3112, Heltec Tracker v1.1/v2). It costs ~7.4 KB of static DRAM
(~9.6 KB on room servers) and up to ~13 KB transiently per publish, and caps the
table at 20 entries — so the feature is deliberately **not** enabled on the classic
ESP32 T-LoRa V2.1–1.6 observer builds, which are already down to one active TLS
slot. Publishing peaks while the table is built, so keep the slot guidance above
in mind: the peak lands on the same internal heap the TLS stack draws from.

## Build Configuration

To build the MQTT bridge firmware:

```bash
# Heltec V3
pio run -e Heltec_v3_repeater_observer_mqtt

# Heltec V4
pio run -e heltec_v4_repeater_observer_mqtt

# Heltec Wireless Tracker v1.1 / v2
pio run -e heltec_tracker_v1_1_repeater_observer_mqtt
pio run -e heltec_tracker_v1_1_room_server_observer_mqtt
pio run -e heltec_tracker_v2_repeater_observer_mqtt
pio run -e heltec_tracker_v2_room_server_observer_mqtt

# Station G2
pio run -e Station_G2_repeater_observer_mqtt

# Station G3 (ESP32)
pio run -e Station_G3_ESP32_repeater_observer_mqtt
pio run -e Station_G3_ESP32_room_server_observer_mqtt

# LilyGo T-LoRa V2.1-1.6 (TTGO LoRa32 V1.0)
pio run -e LilyGo_TLora_V2_1_1_6_repeater_observer_mqtt
pio run -e LilyGo_TLora_V2_1_1_6_room_server_observer_mqtt

# Elecrow ThinkNode M7
pio run -e ThinkNode_M7_repeater_observer_mqtt
pio run -e ThinkNode_M7_room_server_observer_mqtt
```

**ThinkNode M7 — WiFi only:** the M7 has an onboard CH390 Ethernet controller, and
`ThinkNode_M7_companion_radio_ethernet` uses it, but the MQTT bridge's link
management is bound to the WiFi station API, so the observer envs uplink over WiFi.
See `UPSTREAM_BUGS.md` for the Ethernet gap. The board has PSRAM, so these builds
get neighbors publication (`WITH_MQTT_NEIGHBORS`) automatically.

**TLora naming:** The env prefix `LilyGo_TLora_V2_1_1_6` is LilyGo’s **T-LoRa V2.1–1.6** board (SX1276); PlatformIO selects **`ttgo-lora32-v1`** (TTGO LoRa32 V1.0). **MQTT observer** envs extend a slim base **without** `sensor_base` so the image fits `min_spiffs`; **all other** `LilyGo_TLora_V2_1_1_6_*` targets still use optional I2C environmental sensors as before. The **`lilygo_tlora_c6`** variant is separate hardware (ESP32-C6).

**T-LoRa V2.1–1.6 MQTT observer — one WSS broker:** This hardware is **classic ESP32 without PSRAM**. Each WSS preset uses a full TLS stack and large contiguous heap allocations; **two active broker presets at once** typically fails the second connection (`mbedtls_ssl_setup` / `esp-tls` `0x8017`, low `IntMax` in `memory`). **Treat these observer builds as supporting one active cloud preset:** configure the broker you need in `mqtt1` or `mqtt2`, and set the other slot to `none` (e.g. `set mqtt2.preset none`). Use PSRAM-capable boards if you need multiple simultaneous MQTT uplinks.

### Partition Table Changes — Merged Firmware Required

Some MQTT observer builds use a non-default partition table to accommodate the larger firmware size (MQTT libraries, TLS, cert bundle, etc.). **When a board's partition table changes, you must flash the merged firmware (`*-merged.bin`) the first time** so the new partition layout and bootloader are written together. After that initial flash, standard OTA or non-merged updates will work normally.

| Environment | Partition Table | Flash Size | App Slot Size | Notes |
|-------------|----------------|------------|---------------|-------|
| `LilyGo_T3S3_sx1262_repeater_observer_mqtt` | `min_spiffs.csv` | 4 MB | 1.875 MB | Changed from default (1.25 MB) |
| `LilyGo_T3S3_sx1262_room_server_observer_mqtt` | `min_spiffs.csv` | 4 MB | 1.875 MB | Changed from default (1.25 MB) |
| `LilyGo_TLora_V2_1_1_6_repeater_observer_mqtt` | `min_spiffs.csv` | 4 MB | 1.875 MB | TTGO LoRa32 V1.0; observer omits `sensor_base`. See the one-WSS-broker note above. |
| `LilyGo_TLora_V2_1_1_6_room_server_observer_mqtt` | `min_spiffs.csv` | 4 MB | 1.875 MB | same |
| `Station_G2_repeater_observer_mqtt` | `default_16MB.csv` | 16 MB | 6.25 MB | 16 MB flash board |
| `Station_G2_room_server_observer_mqtt` | `default_16MB.csv` | 16 MB | 6.25 MB | 16 MB flash board |
| `Station_G3_ESP32_repeater_observer_mqtt` | `default_16MB.csv` | 16 MB | 6.25 MB | 16 MB flash board |
| `Station_G3_ESP32_room_server_observer_mqtt` | `default_16MB.csv` | 16 MB | 6.25 MB | 16 MB flash board |
| `LilyGo_TBeam_1W_repeater_observer_mqtt` | `default_16MB.csv` | 16 MB | 6.25 MB | Set in `boards/t_beam_1w.json`; required vs implicit `default.csv` |
| `LilyGo_TBeam_1W_room_server_observer_mqtt` | `default_16MB.csv` | 16 MB | 6.25 MB | same |

Boards absent from this table need no special first flash: their observer envs use the
same partition table as the board's other firmwares.

**Settings loss when the layout changes**

A merged image writes a new bootloader **and** partition table at offset `0x0`. If that
layout differs from what the device already has, **NVS is typically wiped** — expect to lose
stored configuration (admin preferences, WiFi, MQTT slots, name) and reconfigure from
scratch. If the layout is unchanged, NVS is usually retained, though Bluetooth pairings may
still be cleared on some upgrade paths.

Two cases catch people out: the TLora observer builds use the same `min_spiffs.csv` as other
MeshCore TLora builds, so normal repeater ↔ room server ↔ observer moves are fine — but
coming *from* an older TLora observer that used `huge_app.csv` is a layout change. And any
device previously running a non-MeshCore partition map will be wiped by its first merged
flash regardless of target.

**How to flash the merged firmware:**

- **Web flasher (recommended):** Use the [MeshCore Observer Flasher](https://observer.gessaman.com/) to flash from your browser — no tools to install. Pick **MQTT Observer Firmware** and your device. Its Download menu also serves the individual `*-merged.bin`, erase, and bootloader files if you would rather flash with something else. Requires Chrome or Edge.
- **Command line:**
  ```bash
  # Build the merged binary
  pio run -t mergebin -e LilyGo_T3S3_sx1262_repeater_observer_mqtt

  # Flash at offset 0x0 (overwrites bootloader + partition table)
  esptool.py write_flash 0x0 .pio/build/LilyGo_T3S3_sx1262_repeater_observer_mqtt/firmware-merged.bin
  ```

### Build Flags
- `WITH_MQTT_BRIDGE=1` - Enable MQTT bridge (required)
- `WITH_SNMP=1` - Enable SNMP agent (optional, see [MQTT_SNMP.md](MQTT_SNMP.md))
- `MQTT_DEBUG=1` - Enable debug logging (optional)
- `MQTT_WIFI_TX_POWER` - WiFi TX power level (default: `WIFI_POWER_11dBm`)
- ~~`MQTT_WIFI_POWER_SAVE_DEFAULT`~~ - Removed; all builds now default to `none` (no power save)

#### Compile-time fresh-install defaults (`src/helpers/MQTTDefaults.h`)

Optional PlatformIO `build_flags` override defaults written when `/mqtt_prefs` is first created. They do **not** change existing saved prefs on upgrade or reflash (unless `/mqtt_prefs` is erased).

| Macro | Default | Notes |
|-------|---------|-------|
| `MQTT_DEFAULT_SLOT1_PRESET` … `MQTT_DEFAULT_SLOT6_PRESET` | slots 1–2: `analyzer-us` / `analyzer-eu`; slots 3–6: `none` | Must be a built-in preset name, `none`, or `custom` |
| `MQTT_DEFAULT_IATA` | (empty) | e.g. `'"YYZ"'` |
| `MQTT_DEFAULT_TIMEZONE` | (empty) | e.g. `'"America/Toronto"'` |
| `MQTT_DEFAULT_TIMEZONE_OFFSET` | `0` | Fallback hours when TZ string is empty |

Example community build:

```ini
build_flags =
  -D MQTT_DEFAULT_SLOT1_PRESET='"meshcore-ca-1"'
  -D MQTT_DEFAULT_SLOT2_PRESET='"meshcore-ca-2"'
  -D MQTT_DEFAULT_IATA='"YYZ"'
  -D MQTT_DEFAULT_TIMEZONE='"America/Toronto"'
  -D MQTT_DEFAULT_TIMEZONE_OFFSET=-5
```

WiFi SSID/password are not compile-time configurable (operators set them per device via CLI).

Legacy `get mqtt.analyzer_us` / `set mqtt.analyzer_us` still refer to the preset name `analyzer-us`, not “whatever slot 1 default is”.

## Default Configuration

The MQTT bridge comes with the following defaults for fresh installs (unless overridden by the macros above):
- **Origin**: Device name (set automatically from `set name`)
- **IATA**: (blank — must be configured for MeshCore-style topic presets such as Analyzer and TennMesh, unless `MQTT_DEFAULT_IATA` is set at build time)
- **Status Messages**: Enabled
- **Packet Messages**: Enabled
- **Raw Messages**: Disabled
- **RX Packets**: Enabled (uplink received packets)
- **TX Packets**: `advert` by default (uplink this node's own adverts; set to `on` for all TX or `off` to disable)
- **Status Interval**: 5 minutes (300000 ms)
- **Slot 1**: `analyzer-us`
- **Slot 2**: `analyzer-eu`
- **Slots 3-6**: `none` (disabled)
- **Per-slot packet filters**: `all` (every payload type is uploaded)
- **WiFi SSID**: (blank — must be configured)
- **WiFi Password**: (blank — optional for open networks)
- **WiFi Power Save**: `none` (no power save)
- **Timezone**: (blank — uses UTC until configured, unless `MQTT_DEFAULT_TIMEZONE` is set at build time)
- **Timezone Offset**: 0 (fallback, no offset, unless `MQTT_DEFAULT_TIMEZONE_OFFSET` is set)
- **Repeat (forwarding)**: On (set `repeat off` for receive-only observers)

## CLI Commands

### MQTT Slot Commands

Each slot (1-6) supports the following commands:

#### Get Commands
- `get mqtt1.preset` - Get slot 1 preset name
- `get mqtt2.preset` - Get slot 2 preset name
- `get mqttN.preset` - Get slot N preset name (N = 1-6)
- `get mqttN.server` - Get custom server hostname for slot N
- `get mqttN.port` - Get custom server port for slot N
- `get mqttN.username` - Get custom username for slot N
- `get mqttN.password` - Get custom password for slot N
- `get mqttN.token` - Get per-slot token (e.g., MeshRank account token)
- `get mqttN.topic` - Get custom topic template for slot N
- `get mqttN.audience` - Get JWT audience for slot N (custom slots only)
- `get mqttN.filter` - Get the slot's packet-type allowlist (`all`, `none`, or numeric CSV)

#### Set Commands
- `set mqttN.preset <name>` - Set slot N to a built-in preset. Use any `name` from [Broker Presets](#broker-presets), which also lists the few presets needing extra setup.
- `set mqttN.preset custom` - Set slot N to custom broker (configure server/port/username/password)
- `set mqttN.preset none` - Disable slot N
- `set mqttN.server <hostname>` - Set custom server hostname for slot N
- `set mqttN.port <port>` - Set custom server port for slot N (1-65535)
- `set mqttN.username <username>` - Set username for slot N (`custom` preset, or presets like `inwmesh` that require per-device credentials)
- `set mqttN.password <password>` - Set password for slot N (`custom` preset, or presets like `inwmesh` that require per-device credentials)
- `set mqttN.token <token>` - Set per-slot token (required for MeshRank preset)
- `set mqttN.topic <template>` - Set custom topic template (custom preset only, see below)
- `set mqttN.audience <audience>` - Set JWT audience for custom slot (enables Ed25519 JWT auth)
- `set mqttN.audience` - Clear JWT audience (reverts to username/password auth)
- `set mqttN.filter <all|none|list>` - Select payload types uploaded to this slot

**Note:** Custom server/port settings only apply when the slot's preset is `custom`. Username/password also apply to built-in presets that use per-slot credentials (e.g. `inwmesh`); other userpass presets (`tennmesh`, `nashmesh`, `ctmesh`) ship fixed credentials in firmware.

#### Per-broker packet filters

Each slot has an independent allowlist. List entries may be payload-type names
or numbers, and the two can be mixed. These are all equivalent, sending only
text messages and adverts to slot 1:

```bash
set mqtt1.filter txt_msg,advert
set mqtt1.filter 2,4
set mqtt1.filter advert, 2
```

Use `none` when a broker should remain connected for status/neighbors but
receive no packet traffic. A bare `set mqttN.filter` resets the slot to `all`.
`get mqttN.filter` always answers in the canonical numeric form (`all`, `none`,
or an ascending CSV), whichever spelling was used to set it.

| Type | Name | Type | Name |
|------|------|------|------|
| 0 | `req` | 8 | `path` |
| 1 | `response` | 9 | `trace` |
| 2 | `txt_msg` | 10 | `multipart` |
| 3 | `ack` | 11 | `control` |
| 4 | `advert` | 12-14 | reserved (number only) |
| 5 | `grp_txt` | 15 | `raw_custom` |
| 6 | `grp_data` | | |
| 7 | `anon_req` | | |

Names are lowercase and exact; types 12-14 are reserved upstream and have no
name, so they are selectable by number only.

The filter applies to both structured `packets` and `raw` publications for RX
packets and for TX packets permitted by `mqtt.tx`. It does not affect local
packet processing, forwarding, capture logs, status, or neighbors. Changes
apply live without reconnecting the broker.

A rejected packet is dropped before it is copied into the publish queue, so a
narrow filter saves the queue slot and the per-packet work, not just the
upload. `get mqtt.stats` reports the running count as `filt=<n>`, and a slot
whose filter is not `all` shows it in `get mqttN.diag` and on the WebConfig
Stats tab — a filtered slot otherwise looks identical to an idle healthy one.

The diag reply is capped at 160 characters. When a slot is also reporting a
long error tail, the filter is summarised as `filter:<n>/16` rather than
listed, because a list clipped mid-way would read as a different, valid
allowlist. `get mqttN.filter` always gives the exact value.

In WebConfig the allowlist is a checkbox per type under each configured slot,
with **All** / **None** shortcuts. Clearing every box is `none` (nothing
uploaded).

**Downgrade note:** rolling back to any build from this release onward is safe —
the older firmware reads the settings it understands and simply ignores the
packet filters, which revert to `all` if it saves.

Rolling back to a build released *before* this one is the case to watch: that
firmware rejects the longer settings file outright and falls back to defaults,
losing the stored WiFi credentials along with the broker config. Slots left at
the `all` default keep the file in the shorter layout those builds can read, so
if you may need to roll a node back that far, reset every slot to `all` first.


#### Example: MeshRank

MeshRank needs an account token, generated on the MeshRank website and tied to your account:
```bash
set mqtt3.preset meshrank
set mqtt3.token FE1B34242C5938C39225310081FD6718
```

It receives status, packets, and neighbors under `meshrank/uplink/{token}/{device}/`, using
the same type suffixes as the MeshCore layout. Raw is **not** sent to MeshRank — it is the
highest-volume topic and the broker does not consume it — so `set mqtt.raw on` has no effect
on a MeshRank slot. Its broker does not accept the retain flag, so those publishes go out
unretained.

### Custom Brokers

Set the preset to `custom` and supply the broker address, plus credentials in whichever style
the broker expects.

**Username/password:**
```bash
set mqtt3.preset custom
set mqtt3.server your-broker.example.com
set mqtt3.port 1883
set mqtt3.username your-username
set mqtt3.password your-password
```

**Ed25519 JWT** — for community brokers implementing the same JWT auth protocol as the
built-in presets. Setting `audience` is what switches the slot to JWT:
```bash
set mqtt3.preset custom
set mqtt3.server wss://my-broker.example.com:443/mqtt
set mqtt3.audience my-broker.example.com
```

With `audience` set, the device connects as `v1_{PUBLIC_KEY}` with an Ed25519-signed JWT as
the password, renews tokens before expiry (default 24h lifetime), and includes the owner
public key and email in the JWT payload if `set mqtt.owner` / `set mqtt.email` are
configured. Clear it with a bare `set mqtt3.audience` to revert to username/password.

**Local development broker** — a LAN broker with no SSL termination. Non-TLS transports
(`ws://`, `mqtt://`) skip certificate verification entirely:
```bash
set mqtt3.preset custom
set mqtt3.server ws://192.168.1.50:9001/mqtt
```

**Custom topic layout** — see [Custom Topic Templates](#custom-topic-templates) below:
```bash
set mqtt3.topic mynetwork/{device}/{type}
```

When the server is given as a full URL with a scheme (`mqtt://`, `mqtts://`, `ws://`,
`wss://`), `set mqttN.port` is optional — an explicit port in the URL is used as-is, and
without one the scheme's default port applies.

### Custom Topic Templates

When a slot's preset is `custom`, you can define a custom topic template using placeholders:

| Placeholder | Value | Example |
|-------------|-------|---------|
| `{iata}` | IATA airport code | `SEA` |
| `{device}` | Device public key (64 hex chars) | `CC5D3CFD...` |
| `{token}` | Per-slot token from `mqttN.token` | `FE1B3424...` |
| `{type}` | Message type | `status`, `packets`, or `raw` |

If no custom topic is set, custom slots default to: `meshcore/{iata}/{device}/{type}`

**Note:** Topic templates only apply to `custom` preset slots. Built-in presets (analyzer-us, analyzer-eu, meshmapper, meshrank, eastidahomesh, coloradomesh, tennmesh, etc.) always use their hardcoded topic format.

### MQTT Shared Commands

These settings apply across all MQTT slots:

#### Get Commands
- `get mqtt.origin` - Get device origin name
- `get mqtt.iata` - Get IATA code
- `get mqtt.presets` - List available MQTT presets (paginated, comma-separated)
- `get mqtt.presets <start>` - Continue list from index shown in `... next:<idx>`
- `get mqtt.status` - Get MQTT status summary (connection info per slot, plus the periodic neighbors schedule when `mqtt.neighbors` is on)
- `get mqtt.packets` - Get packet message setting (on/off)
- `get mqtt.raw` - Get raw message setting (on/off)
- `get mqtt.rx` - Get RX packet uplinking setting (on/off)
- `get mqtt.tx` - Get TX packet uplinking setting (on/off/advert)
- `get mqtt.interval` - Get status publish interval
- `get mqtt.neighbors` - Get periodic neighbors publishing setting (on/off; neighbors-enabled builds)
- `get mqtt.neighbors.interval` - Get neighbors publish interval in hours (neighbors-enabled builds)
- `get mqtt.ntp` - Get effective NTP server hostname
- `get mqtt.ntp.diag` - Probe every configured NTP server for connectivity (does not change the clock; serial console shows each server's reported time, LoRa shows a compact `<server> ok|fail` list)
- `get mqtt.owner` - Get owner public key (serial console only)
- `get mqtt.email` - Get owner email address (serial console only)

#### Set Commands
- `set mqtt.origin <name>` - Set device origin name
- `set mqtt.iata <code>` - Set IATA code (auto-uppercased)
- `set mqtt.status on|off` - Enable/disable status messages
- `set mqtt.packets on|off` - Enable/disable packet messages
- `set mqtt.raw on|off` - Enable/disable raw messages
- `set mqtt.rx on|off` - Enable/disable RX (received) packet uplinking
- `set mqtt.tx on|off|advert` - Set TX packet uplinking mode:
  - `on` - Uplink all transmitted packets
  - `advert` - Uplink only this node's own advert packets (self-originated)
  - `off` - Disable TX packet uplinking
- `set mqtt.interval <minutes>` - Set status publish interval (1-60 minutes)
- `set mqtt.neighbors on|off` - Enable/disable periodic neighbors publishing (neighbors-enabled builds; read live, no restart)
- `set mqtt.neighbors.interval <hours>` - Set neighbors publish interval (12-336 hours, default 24; neighbors-enabled builds)
- `set mqtt.ntp <hostname>` - Set custom NTP server (validated with immediate sync); `none` reverts to default
- `set mqtt.owner <64-hex-char-public-key>` - Set owner public key
- `set mqtt.email <email>` - Set owner email address

### WiFi Commands

#### Get Commands
- `get wifi.ssid` - Get WiFi SSID
- `get wifi.pwd` - Get WiFi password
- `get wifi.status` - Get WiFi connection status, IP, RSSI, and uptime
- `get wifi.powersave` - Get WiFi power save mode (none/min/max)

#### Set Commands
- `set wifi.ssid <ssid>` - Set WiFi SSID
- `set wifi.pwd <password>` - Set WiFi password
- `set wifi.powersave none|min|max` - Set WiFi power save mode

> **Note:** The value is everything after the first space (spaces in SSID/password are fine). Do not wrap in quotes — they are stored literally. Max length: 31 characters (SSID), 63 (password). For open networks, use `set wifi.pwd ` with nothing after the space.
  - `none` - No power saving (best performance, highest power consumption)
  - `min` - Minimum power saving (balanced performance and power)
  - `max` - Maximum power saving (lowest power consumption, may affect performance)

### Timezone Commands

#### Get Commands
- `get timezone` - Get timezone string (e.g., "America/Los_Angeles")
- `get timezone.offset` - Get timezone offset in hours (-12 to +14)

#### Set Commands
- `set timezone <string>` - Set timezone string (IANA format or abbreviation)
- `set timezone.offset <offset>` - Set timezone offset in hours (-12 to +14)

#### Supported Timezone Formats
- **IANA strings**: `America/Los_Angeles`, `Europe/London`, `Asia/Tokyo`, etc.
- **Common abbreviations**: `PDT`, `PST`, `MDT`, `MST`, `CDT`, `CST`, `EDT`, `EST`, `BST`, `GMT`, `CEST`, `CET`
- **UTC offsets**: `UTC-8`, `UTC+5`, `+5`, `-8`, etc.

### Device & Radio Commands

These are standard MeshCore commands, not MQTT-specific, but important for observer setup:

#### Get Commands
- `get name` - Get device name
- `get repeat` - Get repeat (forwarding) status (on/off)
- `get freq` - Get radio frequency
- `get public.key` - Get device public key (for migration)

#### Set Commands
- `set name <name>` - Set device name (also sets MQTT origin)
- `set repeat on|off` - Enable/disable packet forwarding (use `off` for receive-only observers)
- `set prv.key <64-hex-char-key>` - Restore private key (for migrating identity from another device)
- `set tx <dBm>` - Set transmit power

### Bridge Commands

#### Get Commands
- `get bridge.source` - Get packet source (rx/tx)
- `get bridge.enabled` - Get bridge enabled status (on/off)

#### Set Commands
- `set bridge.source rx|tx` - Set packet source (rx for received, tx for transmitted)
- `set bridge.enabled on|off` - Enable/disable bridge

> **Note:** `bridge.enabled` is the master switch for the whole bridge system. `bridge.source`
> applies to non-MQTT bridges (RS232, ESP-NOW) only — for MQTT use `mqtt.rx` and `mqtt.tx`,
> which control each direction independently.

### SNMP Commands

Observer nodes include an optional SNMP v2c agent that exposes radio stats, MQTT
connectivity, memory usage, and network information to standard monitoring tools.

#### Get Commands
- `get snmp` - Get SNMP agent status (on/off)
- `get snmp.community` - Get SNMP community string

#### Set Commands
- `set snmp on|off` - Enable/disable SNMP agent (restart required)
- `set snmp.community <string>` - Set SNMP community string (restart required, default: `public`)

See [MQTT_SNMP.md](MQTT_SNMP.md) for setup and the full OID reference.

### Web Configuration Portal

The observer builds include a browser-based configuration portal so a node can
be provisioned and managed without the serial CLI. It is started from the CLI
(serial or remote admin) and is never on by default on a configured node.

#### CLI commands
- `start webconfig` — start the portal. If WiFi is already configured and
  connected, it binds to the node's **LAN** IP and requires the admin password
  to log in. If WiFi is **not** configured (`wifi.ssid` empty), it raises the
  setup AP instead (same as first boot).
- `start webconfig ap` — force the **setup AP** even when WiFi is configured.
  The MQTT bridge must be stopped first (`set bridge off`); the AP owns the
  radio. Used for re-provisioning in the field.
- `stop webconfig` — stop the portal and free its resources. LAN mode runs until
  this is issued; the setup AP also auto-stops after an idle timeout (default 10
  minutes with no station associated).

#### First-boot / setup-AP behavior
On a node with no WiFi configured, the portal comes up automatically as an open
SoftAP named `MeshCore-Setup-XXXX` (last two bytes of the public key), with a
captive-portal redirect. The device display shows the AP name and portal URL
(`http://192.168.4.1/`). Walk through the wizard (WiFi → radio → MQTT → review),
then **Save & reboot**; the node reboots and joins the configured network.

Optionally set a WPA2 password for the setup AP at build time with
`-D WEBCONFIG_AP_PASSWORD='"yourpassword"'`.

#### Modes and authentication
- **Setup AP**: unauthenticated. Trust is based on physical proximity to the
  open/PSK AP. Only the SoftAP interface serves the API — on `start webconfig
  ap` the STA is explicitly disassociated so the API is **not** exposed on the
  LAN the node was attached to.
- **LAN**: requires the admin password (same one used for remote CLI admin).
  Sessions use a cookie with a sliding idle expiry (default 20 minutes); five
  failed logins trigger a 30-second lockout.

> **Security note:** the open setup AP transports WiFi/MQTT credentials over
> plain HTTP. Provision on a trusted, non-public frequency/location, set
> `WEBCONFIG_AP_PASSWORD` where feasible, and prefer LAN mode for ongoing
> management. The setup AP is intended for initial provisioning, not
> long-running operation.

#### Applying changes
- **Radio** (freq/BW/SF/CR): persisted but applied only on reboot; the UI shows
  a "reboot to apply" hint.
- **WiFi SSID/password**: changing these in LAN mode saves and reboots so the
  node reconnects on the new network (the page will drop; find the new IP on
  your router). In the setup wizard, saving always reboots.
- **MQTT publishing toggles / slot config**: applied live to the running bridge
  (no reboot needed).
- **NTP server**: saved immediately; the time sync runs in the background —
  verify with `get mqtt.ntp.diag`.

#### Recovery
If provisioning fails or you're locked out of the portal, connect over USB
serial and use the CLI directly (e.g. `set wifi.ssid ...`, `set wifi.pwd ...`,
`get wifi.status`, `stop webconfig`). Serial access always works regardless of
the portal state.

### Local testing without hardware

Two ways to iterate on observer/WiFi functionality without flashing a device:

- **Portal UI** — run the mock backend and open the real portal in a browser:
  `python3 scripts/webconfig_mock_server.py` (add `--setup` for the first-boot
  wizard), then browse to `http://localhost:8080/`. It serves `webui/index.html`
  and mirrors the firmware's `/api/*` contract (reqid handshake, reboot gating,
  validation, secret masking), so the portal JS runs against realistic
  responses. Stdlib only; no account.
- **Boot / WiFi / MQTT / CLI / OLED** — the Wokwi ESP32-S3 sim. Build
  `pio run -e Heltec_v3_repeater_observer_mqtt_sim -t mergebin` (LoRa radio
  stubbed via `SimRadio`, WiFi pre-seeded to `Wokwi-GUEST`), then run the sim
  from `wokwi.toml`/`diagram.json` (VS Code Wokwi extension or `wokwi-cli`).
  Outbound MQTT works on the free gateway; incoming (browser → on-device portal)
  needs Wokwi's paid Private Gateway — use the mock backend above for portal UI.

Backend handler logic is covered by host unit tests under `test/` (`pio test -e
native`); see [test/README.md](test/README.md) for the suites and how to run them.


## MQTT Topics

The bridge publishes to four main topics with the following structure:

### Status Topic: `meshcore/{IATA}/{DEVICE_PUBLIC_KEY}/status`
Device connection status and metadata, QoS 1. Retained, except on presets whose broker rejects the retain flag (`meshrank`, `waev`).

### Packets Topic: `meshcore/{IATA}/{DEVICE_PUBLIC_KEY}/packets`
Full packet data with RF characteristics and metadata.

### Raw Topic: `meshcore/{IATA}/{DEVICE_PUBLIC_KEY}/raw`
Minimal raw packet data for map integration.

### Neighbors Topic: `meshcore/{IATA}/{DEVICE_PUBLIC_KEY}/neighbors`
Cached zero-hop repeater neighbors with SNR, last-heard age, and flood-allowed scopes. Published on `discover.scopes` or periodically when `mqtt.neighbors` is enabled (observer builds with neighbors compiled in; non-PSRAM builds cap the table at 20 entries and set `truncated`). Goes to every configured slot's `neighbors` topic at QoS 0, retained only where the preset allows it.

Periodic publishing first runs a 60-second zero-hop neighbor refresh equivalent to `discover.neighbors`, then queries the refreshed table for scopes and publishes when the scope-query phase completes.

Manual `discover.scopes` normally queries the current cache in one shot. If a `discover.neighbors` refresh is already collecting responses — whether started from the CLI or by the periodic timer — the scope queries are queued behind its 60-second window instead, so they run against the refreshed table. The reply reports the wait, e.g. `OK - scopes queued (47s discovery remaining)`. A queued one-shot request survives `set mqtt.neighbors off`; only the periodic timer's own refresh is cancelled by it.

While `mqtt.neighbors` is on, `get mqtt.status` appends `nbr: <next>/<last>` — time to the next automatic publish (`3h12m`, `12m`, `45s`, or `active`/`due`) and the last publish result (`ok`, `failed`, or `none`).

**Note**: `{DEVICE_PUBLIC_KEY}` is the device's public key in hexadecimal format (64 characters). MeshRank slots use `meshrank/uplink/{token}/{DEVICE_PUBLIC_KEY}/neighbors` instead.

## JSON Message Formats

### Status Message
```json
{
  "status": "online|offline",
  "timestamp": "2024-01-01T12:00:00.000000+00:00",
  "origin": "Device Name",
  "origin_id": "DEVICE_PUBLIC_KEY",
  "model": "device_model",
  "firmware_version": "firmware_version",
  "radio": "radio_info",
  "client_version": "meshcore/{firmware_version}",
  "repeat": "on|off",
  "stats": {
    "battery_mv": 4100,
    "uptime_secs": 3600,
    "packets_sent": 42,
    "packets_received": 128,
    "errors": 0,
    "queue_len": 0,
    "noise_floor": -110,
    "tx_air_secs": 12,
    "rx_air_secs": 340,
    "recv_errors": 2,
    "internal_heap": 102400
  }
}
```

**Notes:**
- Timestamps are always emitted in UTC with an explicit `+00:00` offset.
- The `stats` object is only included when at least one stat value is available; individual fields are omitted when their value is unavailable.
- `packets_sent` / `packets_received` are cumulative totals since boot (flood + direct), sourced from the dispatcher counters.

### Packet Message
```json
{
  "origin": "MeshCore-HOWL",
  "origin_id": "A1B2C3D4E5F67890...",
  "timestamp": "2024-01-01T12:00:00.000000+00:00",
  "type": "PACKET",
  "direction": "rx|tx",
  "time": "12:00:00",
  "date": "01/01/2024",
  "len": "45",
  "packet_type": "4",
  "route": "F|D|T|U",
  "payload_len": "32",
  "raw": "F5930103807E5F1E...",
  "SNR": "12.5",
  "RSSI": "-65",
  "score": "234",
  "hash": "A1B2C3D4E5F67890",
  "path": ["aa", "bb", "cc"]
}
```

**Notes:**
- All numeric fields (`len`, `packet_type`, `payload_len`, `SNR`, `RSSI`, `score`) are formatted as JSON strings.
- `time` and `date` are always UTC (`HH:MM:SS` and `DD/MM/YYYY`); `timestamp` is UTC with an explicit `+00:00` offset.
- `SNR`, `RSSI`, and `score` are only present for RX packets (received from radio). TX packets omit these fields since the packet originates from this node.
- `score` is the firmware's rebroadcast score for the received packet (the same value used to compute flood-rebroadcast delay), scaled ×1000 to match the integer printed in the serial RX log — e.g. a score of `0.234` is emitted as `"234"` (range `0`–`1000`). Omitted when unavailable (e.g. the non-PSRAM reconstruction-less fallback path).
- `path` is only present for direct-route packets that carry path data. It is a JSON array of lowercase hex hop tokens, one element per hop — e.g. `["aa","bb","cc"]` for single-byte hashes, or `["aaaa","bbbb"]` for multi-byte hashes. This matches the `path` representation emitted by [meshcore-packet-capture](https://github.com/agessaman/meshcore-packet-capture).

### Raw Message
```json
{
  "origin": "MeshCore-HOWL",
  "origin_id": "A1B2C3D4E5F67890...",
  "timestamp": "2024-01-01T12:00:00.000000+00:00",
  "type": "RAW",
  "data": "F5930103807E5F1E..."
}
```

### Neighbors Message (neighbors-enabled observer builds)
```json
{
  "timestamp": "2024-01-01T12:00:00.000000+00:00",
  "origin": "MeshCore-HOWL",
  "origin_id": "A1B2C3D4E5F67890...",
  "total_neighbors": 2,
  "queried_neighbors": 2,
  "truncated": false,
  "self": { "scopes": "DEN,APRS", "default_scope": "*" },
  "neighbors": [
    {
      "pubkey": "0011223344556677...",
      "snr": 9.75,
      "heard_secs_ago": 42,
      "scopes": "DEN,APRS",
      "status": "responded"
    },
    {
      "pubkey": "8899AABBCCDDEEFF...",
      "snr": 12.5,
      "heard_secs_ago": null,
      "scopes": "DEN",
      "status": "responded"
    }
  ]
}
```
Entries are ordered most- to least-useful (usable age first, then most recently
heard, then stronger SNR); the tail is dropped if the payload would exceed the
10 KB publish buffer. `status` is `responded`, `timeout`, or `send_failed` per
neighbor.

`total_neighbors` is the size of the neighbor-table snapshot the cycle started
from, `queried_neighbors` how many scope requests were confirmed transmitted, and
`truncated` whether the buffer filled before every entry fit. All three are
always present. The `neighbors` array can therefore be shorter than
`total_neighbors` — compare its length against that field rather than assuming
the table is complete.

`heard_secs_ago` is `null`, as in the second entry above, when the age cannot be
computed: the neighbor was last heard before the clock was set, so the stored
stamp and the current clock come from different epochs and their difference is
meaningless (see `UPSTREAM_BUGS.md` #1). **Consumers must treat `null` as
unknown, not as zero** — the key is always present, so a missing key means an
older firmware, and a `null` never means "heard just now". A neighbor that
answers the scope query has its stamp refreshed, so a `null` age normally clears
itself on the next publish cycle.

`self.default_scope` is the region name this node floods to by default (`region
default`); it is `*` when no default region is set, matching the unscoped flood
the radio actually performs in that case.

## Key Features

### Connection Handling
- Automatic reconnection with exponential backoff per slot; a slot that stays down through
  the full backoff ladder is retried on a slow periodic probe instead of hammering the broker
- Packets are queued while a slot is disconnected and flushed when it recovers

### Raw Radio Data Capture
- Captures actual raw radio transmission data (including radio headers)
- Provides accurate SNR/RSSI values from actual radio reception (RX packets only)
- Independent RX and TX packet uplinking — both can be active simultaneously
- TX advert mode: selectively uplink only this node's own advert packets

### Timezone Support
- Accepts IANA timezone strings, common abbreviations, and UTC offsets, with automatic DST handling
- Note: all published MQTT timestamps are UTC regardless of the configured timezone

### WiFi Configuration
- Runtime WiFi credential management via CLI
- Persistent storage across reboots
- Automatic reconnection with exponential backoff

### NTP Time Synchronization
- Automatic time synchronization with NTP servers (required for JWT authentication)
- Default primary: `pool.ntp.org`; built-in fallbacks (tried sequentially on failure): `time.google.com`, `time.cloudflare.com`, `time.aws.com`, `time.nist.gov`
- Periodic time updates (every hour) on the effective primary only; system time is kept in UTC
- Configure and diagnose with `set mqtt.ntp` / `get mqtt.ntp` / `get mqtt.ntp.diag` — see [MQTT Shared Commands](#mqtt-shared-commands)

### Authentication
The auth mode is fixed per preset (see [Broker Presets](#broker-presets)). Three modes are used:
- **JWT Authentication**: Ed25519-signed tokens for brokers that expect JWT (most WSS presets). For `custom` slots, JWT is used when `audience` is set.
- **Username/Password**: Some presets ship fixed credentials embedded in firmware (`tennmesh`, `nashmesh`, `ctmesh` — plain MQTT, no TLS); others (`inwmesh`, `custom`) take per-slot credentials via `mqttN.username` / `mqttN.password`.
- **None**: `meshrank` (account token carried in the topic) and `eastidahomesh` connect without broker auth.
- **Username Format** (JWT): `v1_{UPPERCASE_PUBLIC_KEY}`
- **Automatic Token Renewal**: Tokens are renewed before expiration

## Migration from Old Configuration

Upgrading from firmware that used an older settings layout — including the pre-slot format
(`mqtt.analyzer.us`, `mqtt.server`, …) — needs no manual intervention: the device converts
its stored configuration on the first boot after the update and keeps your brokers, origin,
IATA, message types, WiFi, and timezone. Verify with `get mqtt.status` afterwards.

The one exception is firmware old enough to predate the separate observer settings file: on
that upgrade path the MQTT slot and WiFi configuration cannot be recovered and must be
re-entered. For the per-format details, see
[MQTT_INTERNALS.md](MQTT_INTERNALS.md#settings-upgrade--migration).

## Troubleshooting

#### Device Won't Connect to WiFi
```
get wifi.ssid
get wifi.pwd
set wifi.powersave none    # Try disabling power saving
reboot
```

#### No MQTT Messages Appearing
```
get bridge.enabled
set bridge.enabled on
get mqtt.rx                # Should be "on"
set mqtt.rx on
get mqtt.status            # Check per-slot connection status
get mqtt1.diag             # Last slot error details (TLS/sock/time)
get mqtt2.diag
get mqtt3.diag
get mqtt1.preset           # Verify slots are configured
get mqtt.iata              # IATA must be set for MeshCore-topic presets (e.g. Analyzer, ColoradoMesh, TennMesh)
```

#### Timezone Issues
```
get timezone
get timezone.offset
```
See [Supported Timezone Formats](#supported-timezone-formats) for the accepted values.
Note that published timestamps are UTC regardless of this setting.

## Fault Alerts

Fault alerts broadcast LoRa group-channel notifications when WiFi or configured MQTT links stay down past configured thresholds, with optional recovery notices and rate limiting to avoid spam.
For configuration, CLI commands, examples, and operational notes, see [ALERTS.md](ALERTS.md).

## Radio Watchdog

The radio watchdog detects a LoRa radio that appears stuck in RX mode but has stopped seeing any activity (valid packets, radio interrupts, or successful TX). When the configured silence interval is exceeded, the firmware idles the radio and restarts receive mode. This helps long-running MQTT observers recover from conditions such as PSRAM starvation that can cause missed radio interrupts without a full reboot.

Activity is tracked from the most recent of: a valid RX, any radio ISR (including CRC errors), or a successful TX. That composite timestamp reduces false recoveries on quiet meshes where legitimate packet gaps can exceed the watchdog interval.

#### Get Commands
- `get radio.watchdog` - Get watchdog interval in minutes (`0` = disabled)

#### Set Commands
- `set radio.watchdog <minutes>` - Set watchdog interval (`0` to disable, or `1-120`)

**Default:** `5` minutes

**Examples:**
```bash
get radio.watchdog
set radio.watchdog 10    # 10-minute silence before recovery
set radio.watchdog 0     # disable watchdog
```

On very quiet meshes where no traffic is expected for long periods, increase the interval or set `0` to disable the watchdog and avoid unnecessary radio recoveries.

## Developer Documentation

For source layout, the seams that isolate the observer feature from upstream MeshCore code, and on-device settings migration across firmware versions, see [MQTT_INTERNALS.md](MQTT_INTERNALS.md).
