# USB Chatbot Bridge

A minimal MeshCore node that bridges a single group chat channel to USB serial using JSON. Messages received on the mesh channel are emitted as JSON lines over serial; JSON commands from serial update settings, send messages, and set time.

## Features
- Single group channel configured by hex secret (16 or 32 bytes)
- Node name and channel name persisted in `/usb_chatbot.json`
- RTC set via JSON command
- First inbound channel message auto-sets RTC if the node is still on 2024 (uses sender timestamp)
- No telemetry, sensors, pings, or DM handling to keep firmware small

## Build & Flash (Xiao S3 WIO)
1. Ensure PlatformIO is installed.
2. Build: `pio run -e Xiao_S3_WIO_usb_chatbot`
3. Flash: `pio run -e Xiao_S3_WIO_usb_chatbot -t upload`
4. Monitor: `pio device monitor -b 115200`

If `.pio/libdeps` is cleaned, reapply the CayenneLPP ArduinoJson patch as noted in the build logs (switch `add<JsonObject>()` to `createNestedObject()` in CayenneLPP.cpp) and ensure `densaugeo/base64` and `ArduinoJson` stay in `lib_deps`.

## Serial JSON Protocol (newline-delimited)
Send one JSON object per line at 115200 baud.

Commands:
- `{"cmd":"get_config"}`
- `{"cmd":"set_config","node_name":"USBChat","channel_name":"Chat","channel_key_hex":"001122..."}`
  - `channel_key_hex` must be 32 or 64 hex chars (16 or 32 bytes)
- `{"cmd":"send","text":"hello mesh"}`
- `{"cmd":"set_time","timestamp":1736812345}` (epoch seconds)
- `{"cmd":"reboot"}` (ESP32 targets only)

Events emitted:
- `config` with fields: `node_name`, `channel_name`, `channel_key_hex`, `channel_ready`, `rtc`, `identity_pub`
- `rx` with `text`, `sender`, `timestamp`, `direct`
- `tx` with `ok` and optional `error`
- `error` with `message`

## On-device Storage
- `/usb_chatbot.json` holds node/channel settings.
- `/identity` (handled by MeshCore IdentityStore) holds the node keypair.

## Quick Test Flow
1. Open serial monitor at 115200.
2. Send `get_config` to see current settings.
3. Set channel key: `set_config` with your `channel_key_hex`.
4. `send` a message; watch for `tx` response.
5. Observe `rx` events when other mesh nodes post to the channel.

## Host Serial Helper Script
A simple Python client is provided: [usb_chatbot_cli.py](usb_chatbot_cli.py).

Prereqs: `python -m pip install pyserial`.

Or install from the included requirements file:

```
python -m pip install -r requirements.txt
```

Examples (default port COM9):
- Get config: `python usb_chatbot_cli.py get-config`
- Set config: `python usb_chatbot_cli.py set-config --node USBChat --channel Chat --key 001122...`
- Send text: `python usb_chatbot_cli.py send --text "hello mesh"`
- Set time (now): `python usb_chatbot_cli.py set-time`
- Reboot device: `python usb_chatbot_cli.py reboot`

The script also prints all incoming JSON lines from the device.
