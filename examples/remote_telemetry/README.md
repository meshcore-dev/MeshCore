# Remote Telemetry Node

This example targets ESP32-based boards with WiFi (for example the Seeed XIAO ESP32S3 WIO and LilyGO T-Beam SX1276) and turns the device into a WiFi-enabled telemetry forwarder. The firmware logs in to configured repeaters, fetches telemetry data and republishes it to an MQTT broker.

## Configuration

- On first boot the node starts the WiFiManager portal that captures WiFi, MQTT credentials, telemetry topics and repeater details and saves them to `/telemetry.json` on SPIFFS.
- Populate `examples/remote_telemetry/credentials.h` with deployment defaults for the MQTT host, username and password; leaving those fields blank in the portal applies these values automatically.
- To change settings later, trigger the configuration portal (e.g. erase `/telemetry.json`) or push updates over MQTT as described below.

## Building

Select an ESP32 WiFi environment such as `env:Xiao_S3_WIO_remote_telemetry` or `env:Tbeam_SX1276_remote_telemetry` from PlatformIO, or run:

```
pio run -e Xiao_S3_WIO_remote_telemetry
```

or

```
pio run -e Tbeam_SX1276_remote_telemetry
```

## Logging

Runtime logging uses `Serial` at 115200 baud. Set the build flag `-D REMOTE_TELEMETRY_DEBUG=0` to silence all debug output for production deployments.

## MQTT Control & Payloads

The node listens for JSON commands on the configured control topic and publishes telemetry and status messages to their respective topics. At runtime the control topic gains a suffix with the first two bytes of the node's public key (e.g. `telemetry/control/364C`) so multiple nodes can share a base topic without collisions.

### Commands to the Node

Send JSON payloads to the MQTT control topic (`settings.mqttControlTopic`). Key operations:

#### Repeater management

```json
{"command": "list_repeaters"}
```

```json
{
  "command": "add_repeater",
  "repeater": {
    "name": "IT-Orp-S omni",
    "password": "",
    "pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
  }
}
```

```json
{
  "command": "update_repeater",
  "repeater": {
    "pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc",
    "password": "",
    "name": "IT-Orp-S omni"
  }
}
```

```json
{
  "command": "remove_repeater",
  "pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
}
```

#### Runtime tuning

```json
{"pollIntervalSeconds": 900}
```

```json
{"timeoutRetrySeconds": 120}
```

```json
{"loginRetrySeconds": 600}
```

```json
{"telemetryTopic": "meshcore/telemetry"}
```

> The telemetry topic update takes effect immediately and is persisted to `/telemetry.json`. The `mqttTelemetryTopic` key is accepted as an alias.

### Responses from the Node

Status messages land on `settings.mqttStatusTopic`, for example:

```json
{
  "event": "repeater_added",
  "detail": "RemoteSite",
  "uptimeMs": 52341,
  "pollIntervalMs": 1800000,
  "timeoutRetryMs": 30000,
  "loginRetryMs": 120000,
  "topics": {
    "telemetry": "meshcore/field/telemetry",
    "status": "meshcore/field/status",
    "control": "meshcore/field/control/364C"
  },
  "node": {
    "pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
  }
}
```

```json
{
  "event": "telemetry_topic_updated",
  "detail": "meshcore/field/telemetry",
  "uptimeMs": 54012
}
```

### Telemetry Payloads

Telemetry is published to the current telemetry topic using the normalised format:

```json
{
  "type": "telemetry",
  "repeater": "IT-Orp-S omni",
  "metrics": {
    "voltage": 4.05,
    "current": 0.009,
    "power": 0.0
  },
  "samples": [
    {"channel": 1, "type": 116, "value": 4.0},
    {"channel": 2, "type": 116, "value": 4.05},
    {"channel": 2, "type": 117, "value": 0.009},
    {"channel": 2, "type": 128, "value": 0.0}
  ],
  "collectedAt": "2026-01-11T15:31:43.408Z",
  "collectedAtText": "15:31 on 11/01/2026"
}
```

`metrics` captures the most recent INA219-style voltage/current/power values when available, while `samples` enumerates the raw Cayenne LPP records forwarded by the repeater.

## Operational Notes

- After NTP synchronisation the node schedules a daily reboot in the 03:00 local-time window to minimise disruption.
- Guest logins are issued first to discover a direct path; the stored admin credentials are only sent once a unicast route is known.
