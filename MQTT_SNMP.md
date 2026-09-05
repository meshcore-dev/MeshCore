# SNMP Agent for MeshCore Observer Firmware

This document describes the optional SNMP v2c agent that allows network monitoring tools to poll MeshCore observer nodes for health and performance metrics.

## Overview

The SNMP agent exposes radio statistics, MQTT connectivity status, memory usage, and network information via standard SNMP v2c GET/GETNEXT/GETBULK operations. It is designed for use with monitoring tools such as Nagios, Zabbix, LibreNMS, PRTG, or any SNMP-capable system.

- **Protocol**: SNMPv2c (read-only, no SET support)
- **Port**: UDP 161 (standard SNMP)
- **Library**: [0neblock/SNMP_Agent](https://github.com/0neblock/Arduino_SNMP)
- **Disabled by default** -- must be explicitly enabled via CLI

## Quick Start

```bash
# Enable SNMP
set snmp on
reboot

# Verify from a host on the same network
snmpwalk -v2c -c public <device-ip> 1.3.6.1.4.1.99999
```

## CLI Commands

| Command | Description |
|---|---|
| `get snmp` | Show SNMP agent status (`on` / `off`) |
| `set snmp on` | Enable SNMP agent (restart required) |
| `set snmp off` | Disable SNMP agent (restart required) |
| `get snmp.community` | Show SNMP community string |
| `set snmp.community <string>` | Set community string (restart required, default: `public`) |

## OID Reference

All OIDs are under the private enterprise subtree `1.3.6.1.4.1.99999` (`.iso.org.dod.internet.private.enterprises.99999`).

### System (.1.x.0)

| OID | Type | Description |
|---|---|---|
| `.1.1.0` | INTEGER | System uptime (seconds) |
| `.1.2.0` | STRING | Firmware version |
| `.1.3.0` | STRING | Node name |

### Radio (.2.x.0)

| OID | Type | Description |
|---|---|---|
| `.2.1.0` | INTEGER | Total packets received |
| `.2.2.0` | INTEGER | Total packets sent |
| `.2.3.0` | INTEGER | Receive errors (CRC failures, etc.) |
| `.2.4.0` | INTEGER | Noise floor (dBm) |
| `.2.5.0` | INTEGER | Last RSSI (dBm) |
| `.2.6.0` | INTEGER | Last SNR (dB x 4) |
| `.2.7.0` | INTEGER | Flood packets sent |
| `.2.8.0` | INTEGER | Direct packets sent |
| `.2.9.0` | INTEGER | Flood packets received |
| `.2.10.0` | INTEGER | Direct packets received |
| `.2.11.0` | INTEGER | Total air time (seconds) |

### MQTT (.3.x.0)

| OID | Type | Description |
|---|---|---|
| `.3.1.0` | INTEGER | Connected MQTT slot count |
| `.3.2.0` | INTEGER | Packet queue depth |
| `.3.3.0` | INTEGER | Skipped publishes (memory pressure) |

### Memory (.4.x.0)

| OID | Type | Description |
|---|---|---|
| `.4.1.0` | INTEGER | Free heap (bytes) |
| `.4.2.0` | INTEGER | Max allocatable block (bytes) |
| `.4.3.0` | INTEGER | Free internal RAM (bytes) |
| `.4.4.0` | INTEGER | Free PSRAM (bytes, 0 if no PSRAM) |

### Network (.5.x.0)

| OID | Type | Description |
|---|---|---|
| `.5.1.0` | INTEGER | WiFi RSSI (dBm) |

## Example Output

```
$ snmpwalk -v2c -c public 192.168.50.95 1.3.6.1.4.1.99999

SNMPv2-SMI::enterprises.99999.1.1.0 = INTEGER: 18        # uptime 18s
SNMPv2-SMI::enterprises.99999.1.2.0 = STRING: "v1.14.1"  # firmware
SNMPv2-SMI::enterprises.99999.1.3.0 = STRING: "Heltec Repeater"
SNMPv2-SMI::enterprises.99999.2.1.0 = INTEGER: 3         # 3 packets received
SNMPv2-SMI::enterprises.99999.2.4.0 = INTEGER: -96       # noise floor -96 dBm
SNMPv2-SMI::enterprises.99999.2.5.0 = INTEGER: -25       # last RSSI -25 dBm
SNMPv2-SMI::enterprises.99999.4.1.0 = INTEGER: 199612    # ~195 KB free heap
SNMPv2-SMI::enterprises.99999.4.2.0 = INTEGER: 188404    # ~184 KB max alloc
SNMPv2-SMI::enterprises.99999.5.1.0 = INTEGER: -22       # WiFi RSSI -22 dBm
```

## Architecture

The SNMP agent runs inside the existing MQTT FreeRTOS task on Core 0 (the WiFi/network core). No additional task is created.

- **Radio stats** are pushed from Core 1 (mesh task) every 2 seconds via `updateRadioStats()`
- **MQTT stats** are updated from Core 0 each MQTT task loop iteration
- **Memory and WiFi stats** are read directly in the SNMP loop on Core 0

The agent starts automatically once WiFi connects and `snmp_enabled = 1` in prefs. It adds approximately 2-3 KB RAM overhead plus per-OID handler storage.

## Build Configuration

SNMP is enabled at compile time with the `WITH_SNMP=1` build flag and included in observer firmware targets:

- `heltec_v3_repeater_observer_mqtt`
- `heltec_v4_repeater_observer_mqtt`
- `Station_G2_repeater_observer_mqtt`

To add SNMP to another observer variant, add the following to its `platformio.ini`:

```ini
build_flags =
  ...
  -D WITH_SNMP=1

build_src_filter =
  ...
  +<helpers/SNMPAgent.cpp>

lib_deps =
  ...
  0neblock/SNMP_Agent
```

## Files

| File | Description |
|---|---|
| `src/helpers/SNMPAgent.h` | SNMP agent wrapper class declaration |
| `src/helpers/SNMPAgent.cpp` | OID registration and stats update implementation |
| `src/helpers/CommonCLI.h` | `NodePrefs` struct (`snmp_enabled`, `snmp_community` fields) |
| `src/helpers/CommonCLI.cpp` | CLI command parsing and prefs persistence |
| `src/helpers/bridges/MQTTBridge.cpp` | SNMP loop integration in `mqttTaskLoop()` |
| `examples/simple_repeater/MyMesh.cpp` | Radio stats push and agent initialization |

## Private Enterprise Number

The OIDs currently use a temporary unregistered enterprise number (`99999`). A proper Private Enterprise Number (PEN) can be registered with IANA at no cost at https://pen.iana.org/pen/PenApplication.page.
