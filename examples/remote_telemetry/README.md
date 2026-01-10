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
