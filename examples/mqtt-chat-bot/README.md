# MQTT Chat Bot Bridge

This example bridges a Meshcore group chat channel to MQTT. It listens for mesh messages on a configured channel and republishes them to MQTT, while relaying MQTT messages back into the same mesh channel.

## Supported targets

- LilyGo T-Beam (SX1276 radio)
- Seeed Studio XIAO ESP32S3 (Wio variant with WiFi)

## Build

Use PlatformIO from the project root.

```bash
pio run -e Tbeam_SX1276_mqtt_chat_bot
pio run -e Xiao_S3_WIO_mqtt_chat_bot
```

> **Note:** PlatformIO caches third-party libraries per environment. After the first build, open the generated CayenneLPP sources at [.pio/libdeps/Tbeam_SX1276_mqtt_chat_bot/CayenneLPP/src/CayenneLPP.cpp](.pio/libdeps/Tbeam_SX1276_mqtt_chat_bot/CayenneLPP/src/CayenneLPP.cpp#L972) and [.pio/libdeps/Xiao_S3_WIO_mqtt_chat_bot/CayenneLPP/src/CayenneLPP.cpp](.pio/libdeps/Xiao_S3_WIO_mqtt_chat_bot/CayenneLPP/src/CayenneLPP.cpp#L972) and replace `root.add<JsonObject>()` with `root.createNestedObject()`. This adapts the vendor library to ArduinoJson 6.21.x and must be re-applied whenever PlatformIO refreshes the cache.

Upload with `pio run -t upload -e <environment>` or use the VS Code PlatformIO UI.

## First boot configuration

1. Power the node. The WiFi captive portal (AP `MeshcoreChatBot`) starts automatically when no saved configuration exists or when the user button is held during boot.
2. Connect to the AP and navigate to `http://192.168.4.1`.
3. Enter:
   - WiFi SSID and password.
   - MQTT broker hostname, port, username, and topics.
   - Mesh node name, channel name, and shared key (hex).
4. Save and allow the device to reboot. The node stores all values in SPIFFS.

### Default values

| Item | Default |
| --- | --- |
| WiFi SSID | `ssid` |
| WiFi password | `password` |
| Mesh node name | `IT-bot-<first 4 pub key bytes>` |
| Mesh channel name | `IT-Telemetry` |
| Mesh channel key | `bf0244470ec8b05c6991f0834532b935` |
| MQTT host | `mqtt.example.com` |
| MQTT username | `chatbot-<first 4 pub key bytes>` |
| MQTT control topic | `meshcore/chatbot/control` |
| MQTT RX topic | `meshcore/chatbot/rx` |
| MQTT TX topic | `meshcore/chatbot/tx` |

## Runtime behaviour

- WiFi credentials are managed by WiFiManager. Holding the user button during boot clears stored WiFi and MQTT passwords and forces the portal.
- Once WiFi connects, the node authenticates to MQTT and subscribes to the control and TX topics.
- Mesh messages from the configured channel are published as JSON on the RX topic.
- MQTT payloads received on the TX topic are transmitted into the mesh channel. Plain text payloads are accepted; JSON objects with a `text` field (and optional `sender`) are also supported.
- All configuration values (except passwords) are queryable and updateable via the control topic.

## MQTT control interface

### Request configuration

Publish to the control topic:

```json
{
  "command": "get_config",
  "correlationId": "cfg-1"
}
```

Example response on the same topic:

```json
{
  "event": "config",
  "source": "chatbot",
  "correlationId": "cfg-1",
  "wifi": {
    "ssid": "office",
    "password": "set"
  },
  "mqtt": {
    "host": "mqtt.example.com",
    "port": 1883,
    "username": "chatbot-1A2B",
    "password": "set",
    "control": "meshcore/chatbot/control",
    "rx": "meshcore/chatbot/rx",
    "tx": "meshcore/chatbot/tx"
  },
  "mesh": {
    "node": "IT-bot-1A2B",
    "channel": "IT-Telemetry",
    "key": "bf0244470ec8b05c6991f0834532b935"
  }
}
```

Passwords are never returned; the string `set` indicates a stored value.

### Update configuration

Publish to the control topic; only include fields you wish to change.

```json
{
  "command": "set_config",
  "correlationId": "cfg-2",
  "data": {
    "wifi": {
      "ssid": "office",
      "password": "newpass"
    },
    "mqtt": {
      "host": "mqtt.example.com",
      "username": "chatbot-1A2B"
    },
    "mesh": {
      "channel": "IT-Telemetry",
      "key": "bf0244470ec8b05c6991f0834532b935"
    }
  }
}
```

Successful updates produce:

```json
{
  "event": "config_updated",
  "source": "chatbot",
  "correlationId": "cfg-2"
}
```

Failures emit an `error` event with a `code` and optional `detail`.

## MQTT message formats

### Mesh → MQTT (RX topic)

```json
{
  "event": "mesh_message",
  "source": "chatbot",
  "channel": "IT-Telemetry",
  "timestamp": 1234567890,
  "direct": false,
  "sender": "Alice",
  "text": "Hello world",
  "raw": "Alice: Hello world"
}
```

### MQTT → Mesh (TX topic)

- Plain text: `Hello mesh team`
- JSON payload:

```json
{
  "text": "Status green",
  "sender": "Bridge"
}
```

## Resetting configuration

Hold the user button while powering on. The node wipes stored WiFi and MQTT passwords, restarts the portal, and restores placeholder defaults where applicable.
