# MeshCore Packet Payload Types Reference

When you see `logRxData` messages with different payload types, here's what they mean:

## Payload Types (from Packet.h)

| Type | Code | Name | Use Case | Notes |
|------|------|------|----------|-------|
| 0x00 | 0 | REQ | Request | Prefixed with dest/src hashes, MAC |
| 0x01 | 1 | RESPONSE | Response to REQ | Prefixed with dest/src hashes, MAC |
| 0x02 | 2 | TXT_MSG | Text message | Direct message between two nodes |
| 0x03 | 3 | ACK | Acknowledgment | Simple acknowledgment |
| 0x04 | 4 | ADVERT | Advertisement | Node advertising its Identity (public key, name, etc.) |
| **0x05** | **5** | **GRP_TXT** | **Group text** | **Channel messages - ENCRYPTED** |
| 0x06 | 6 | GRP_DATA | Group data | Group datagram - ENCRYPTED |
| 0x07 | 7 | ANON_REQ | Anonymous request | Generic request |
| 0x08 | 8 | PATH | Path response | Returned path information |
| 0x09 | 9 | TRACE | Trace path | Path tracing packet |
| 0x0A | 10 | MULTIPART | Multipart | One of a set of packets |
| 0x0F | 15 | RAW_CUSTOM | Raw custom | Custom application packets |

## Route Types (2-bit field)

| Type | Code | Name | Meaning |
|------|------|------|---------|
| 0x00 | 0 | TRANSPORT_FLOOD | Flood mode with transport codes |
| 0x01 | 1 | FLOOD | Flood mode (max 64-byte path) |
| 0x02 | 2 | DIRECT | Direct route (path is supplied) |
| 0x03 | 3 | TRANSPORT_DIRECT | Direct route with transport codes |

## What You're Seeing

When you receive `logRxData` packets with:
- **Type: 5 (GRP_TXT)** → These are group/channel messages that were **NOT decrypted** by the firmware
  - The firmware couldn't find the matching channel subscription
  - The payload is encrypted
  - The logger can see the raw packet but can't decrypt it without the channel secret

- **Type: 2 (TXT_MSG)** → Direct text message between two devices
  - Usually appears as `contactMessage` if properly decoded
  - May also appear in logRxData if packet logging is enabled

- **Type: 4 (ADVERT)** → Node advertisement
  - May appear as `contact` or `simpleAdvert` if properly decoded
  - Contains public key and metadata about a node

## Example Output

```
📡 Raw RX Packet Log:
   SNR: 12.25 dB
   RSSI: -28 dBm
   📦 Packet Header: 0x15
       Route: 1 (FLOOD)
       Type: 5 (GRP_TXT)          ← This is a CHANNEL MESSAGE!
       Ver: 0
       Payload Length: 37
       ℹ️  This is a GROUP TEXT MESSAGE (typical for channels)
       Channel Hash Byte: 0x51
   Packet: 150011518... (37 bytes)
```

This means:
1. A channel/group text message was received
2. Route type FLOOD means it could have come from multiple hops
3. The message is encrypted with a channel secret
4. Channel hash 0x51 is the one-byte identifier for the channel
5. The firmware couldn't decrypt it because the channel isn't subscribed

## Why This Matters

- **GRP_TXT with successful decryption** → Appears as `channelMessage` response
- **GRP_TXT without successful decryption** → Appears as `logRxData` with raw encrypted payload
- **GRP_TXT that firmware doesn't handle** → Also appears as `logRxData`

The key issue is that `searchChannelsByHash()` returns 0 (not found) even when channels are subscribed, so the firmware never attempts decryption.

## Fixing Channel Messages

To see channel messages properly decoded:

1. **Verify firmware implements channel storage** - Check if `searchChannelsByHash()` actually returns stored channels
2. **Ensure channels are subscribed** - Call `setChannel()` or `subscribeHashChannel()` to set them up
3. **Monitor logRxData** - Look for Type: 5 messages to confirm the radio is receiving them
4. **Check firmware debug output** - See if `onGroupDataRecv()` is being called or if decryption is failing

## Additional Resources

- Packet.h - Defines all these constants
- Mesh.cpp line 206 - Group message handling
- Radio firmware - Must implement channel subscription storage
