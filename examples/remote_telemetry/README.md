# Remote Telemetry Node

This example targets ESP32-based boards with WiFi (for example the Seeed XIAO ESP32S3 WIO and LilyGO T-Beam SX1276) and turns the device into a WiFi-enabled telemetry forwarder. The firmware logs in to configured repeaters, fetches telemetry data and republishes it to an MQTT broker.

## Configuration

Populate `examples/remote_telemetry/credentials.cpp` (or replace `credentials.h`) with your own values:

- WiFi SSID and password
- MQTT host, port, username/password and publish topic
- A list of repeaters with their public keys and passwords (guest or admin)

> Tip: keep the real credentials out of version control by excluding `credentials.cpp` or redefining the structures in a local-only file.

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

The node listens for JSON commands on the configured control topic and publishes telemetry and status messages to their respective topics.

### Commands to the Node

Send these payloads to the MQTT control topic:

```
{
	"command": "list_repeaters"
}
```

> Passwords are optional. Supply an empty string (`"password": ""`) when the repeater allows guest logins.

```
{
	"command": "add_repeater",
	"repeater": {
		"name": "IT-Orp-S omni",
		"password": "",
		"pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
	}
}
```

```
{
	"command": "update_repeater",
	"repeater": {
		"pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc",
		"password": "",
		"name": "IT-Orp-S omni"
	}
}
```

```
{
	"command": "remove_repeater",
	"pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
}
```

```
{
	"pollIntervalSeconds": 900
}
```

```
{
	"timeoutRetrySeconds": 120
}
```

```
{
	"loginRetrySeconds": 600
}
```

### Responses from the Node

Status messages are published to the configured status topic. Examples include:

```
{
	"event": "repeater_added",
	"detail": "RemoteSite",
	"uptimeMs": 52341,
	"pollIntervalMs": 1800000,
	"timeoutRetryMs": 30000,
	"loginRetryMs": 120000,
	"node": { "pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc" }
}
```

```
{
	"event": "repeaters_snapshot",
	"detail": "repeater_added",
	"uptimeMs": 53002,
	"repeaters": [
		{
			"name": "IT-Orp-S omni",
			"password": "guest",
			"pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
		}
	],
	"nodePubKey": "cafebabe..."
}
```

Telemetry payloads appear on the telemetry topic:

```
{
	"tag": 123456,
	"received": 6789012,
	"repeater": {
		"name": "IT-Orp-S omni",
		"pubKey": "8f3e2e611bf8469deed5ece8a59249281bca7e891febdd58861a406fbf73a8bc"
	},
	"measurements": [
		{ "channel": 1, "type": 0x02, "label": "voltage", "value": 4.12 },
		{ "channel": 2, "type": 0x67, "label": "temperature", "value": 23.5 }
	]
}
```
