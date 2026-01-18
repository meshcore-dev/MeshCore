# Dogechat Bridge

The Dogechat Bridge enables communication between the [Dogechat](https://dogechat.app) Android app and MeshCore mesh network. Messages sent to the `#mesh` channel in Dogechat are relayed to MeshCore nodes, and vice versa.

## Overview

- **Bridge Direction**: Bidirectional - messages flow both ways between Dogechat and MeshCore
- **Channel**: Only the `#mesh` channel is bridged (hardcoded)
- **Identification**: Messages from Dogechat users appear with a phone emoji prefix on MeshCore nodes
- **Platform**: ESP32 only (requires ESP32 ROM miniz for decompression)

## Supported Boards

The following ESP32 boards have `_companion_radio_usb_dogechat` targets:

| Board | Target Name |
|-------|-------------|
| Heltec LoRa32 V2 | `Heltec_v2_companion_radio_usb_dogechat` |
| Heltec LoRa32 V3 | `Heltec_v3_companion_radio_usb_dogechat` |
| Heltec Wireless Stick Lite V3 | `Heltec_WSL3_companion_radio_usb_dogechat` |
| Heltec CT62 | `Heltec_ct62_companion_radio_usb_dogechat` |
| Heltec Tracker V2 | `heltec_tracker_v2_companion_radio_usb_dogechat` |
| LilyGo T3-S3 (SX1262) | `LilyGo_T3S3_sx1262_companion_radio_usb_dogechat` |
| LilyGo T-Deck | `LilyGo_TDeck_companion_radio_usb_dogechat` |
| LilyGo TLora V2.1 | `LilyGo_TLora_V2_1_1_6_companion_radio_usb_dogechat` |
| Station G2 | `Station_G2_companion_radio_usb_dogechat` |
| Seeed Xiao C3 | `Xiao_C3_companion_radio_usb_dogechat` |
| Seeed Xiao S3 WIO | `Xiao_S3_WIO_companion_radio_usb_dogechat` |
| Ebyte EoRa-S3 | `Ebyte_EoRa-S3_companion_radio_usb_dogechat` |

**Note**: NRF52-based boards are not supported because Dogechat message decompression requires the ESP32 ROM miniz library.

## Build Targets

### USB + Dogechat (`*_companion_radio_usb_dogechat`)

This is the recommended configuration:

- **MeshCore companion app**: Connects via USB serial
- **Dogechat app**: Connects via standalone BLE

This gives the best experience because:
- The MeshCore web app works reliably via USB serial
- Dogechat has dedicated BLE access without sharing with MeshCore

### Building

```bash
# Build for Heltec Wireless Stick Lite V3
pio run -e Heltec_WSL3_companion_radio_usb_dogechat

# Flash
pio run -e Heltec_WSL3_companion_radio_usb_dogechat -t upload
```

## How It Works

### Channel Bridging

1. **Dogechat to MeshCore**: When a Dogechat user sends a message to `#mesh`, the bridge:
   - Receives the message via BLE
   - Decompresses it (Dogechat uses zlib compression)
   - Extracts the sender nickname and message content
   - Sends it to the MeshCore #mesh with a phone emoji prefix

2. **MeshCore to Dogechat**: When a MeshCore node sends a message to the `#mesh` channel, the bridge:
   - Receives the mesh packet
   - Creates a Dogechat MESSAGE packet with the sender name and content
   - Broadcasts it via BLE

### Message Format

Messages from Dogechat appear on MeshCore as:
```
<sender>: message content
```

Messages from MeshCore appear on Dogechat with the MeshCore sender's name in angle brackets.

### Long Messages

MeshCore packets have a ~127-byte payload limit. Long Dogechat messages are automatically split into multiple parts with `[1/N]` indicators.

## Limitations

1. **Only #mesh channel**: Only the `#mesh` hashtag channel is bridged. DMs and other channels are ignored.

2. **Message size**: MeshCore packets are limited to ~127 bytes. Long messages are split into multiple parts.

3. **No end-to-end encryption bridge**: Dogechat's Noise protocol encryption and MeshCore's encryption are separate. Messages are decrypted/re-encrypted at the bridge.

4. **No file/image transfer**: Dogechat file transfers (images, etc.) are not supported on the mesh.

5. **Time synchronization**: The bridge synchronizes its clock from incoming Dogechat packets. If no Dogechat client connects, timestamps may be inaccurate. Time needs to be accurate for Dogechat to work

## Debugging

To enable debug output, use one of these methods:

### Method 1: Environment variable (temporary)

```bash
# Build with debug output enabled
PLATFORMIO_BUILD_FLAGS="-D DOGECHAT_DEBUG=1" pio run -e Heltec_WSL3_companion_radio_usb_dogechat
```

### Method 2: Edit platformio.ini (persistent)

Add `-D DOGECHAT_DEBUG=1` to your environment's build_flags:

```ini
[env:Heltec_WSL3_companion_radio_usb_dogechat]
build_flags =
  ${Heltec_WSL3_companion_radio_usb.build_flags}
  ${dogechat_base.build_flags}
  -D DOGECHAT_DEBUG=1   ; Enable debug output
```

This enables detailed logging of:
- Message parsing
- Relay confirmations
- Peer cache updates
- Fragment reassembly

## Technical Details

### BLE Service

- **Service UUID**: `F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C`
- **Characteristic**: Single read/write/notify characteristic
- **MTU**: 517 bytes (max BLE MTU for large messages)

### Channel Key

The `#mesh` channel uses a hashtag-derived key:
```
SHA256("#mesh")[0:16] = 5b664cde0b08b220612113db980650f3
```

This matches MeshCore's hashtag room key derivation.

### Protocol Support

- Message decompression (zlib via ESP32 ROM miniz)
- Ed25519 message signing
- Peer announcement/discovery
- REQUEST_SYNC handling (sends cached messages)
- Fragment reassembly for long messages
