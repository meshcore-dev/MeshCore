# Channel Subscription Analysis

## Current Issue

Messages from the "Public" channel and hashtag channels like "#aqua-test" are **not being decoded as `channelMessage` responses**. Instead, all messages appear as `logRxData` (raw wire-format packets) when packet logging is enabled in the firmware.

## Why This Happens

### 1. Wire Format vs Response Frame Format

There are two different message formats in MeshCore:

**Response Frame Format** (what the companion radio sends via USB):
```
[Frame Header: '<'][Length LSB][Length MSB][ResponseCode][Payload...]
```
- Used for device responses (0x00-0x16) and push notifications (0x80-0x8D)
- Examples: `RESP_CODE_CHANNEL_MSG_RECV_V3` (0x15), `RESP_CODE_CONTACT_MSG_RECV_V3` (0x13)
- These are what the logger is set up to decode via `messageDecoder.ts`

**Wire Format** (raw packets from the radio hardware):
```
[Header Byte][Payload Length (2B)][Path Length (2B)][Transport Codes?][Path][Payload...]
```
- This is the actual packet structure transmitted by the radio
- Header byte contains: Route Type (2 bits) | Payload Type (4 bits) | Version (2 bits)
- Payload Type 0x05 = `PAYLOAD_TYPE_GRP_TXT` (group text - channels)
- These appear in `logRxData` when packet logging is enabled

### 2. The Firmware Gap

The companion radio firmware has a critical limitation:

**In `Mesh.cpp` line 37-40:**
```cpp
int Mesh::searchChannelsByHash(const uint8_t* hash, GroupChannel channels[], int max_matches) {
  return 0;  // not found - THIS IS A STUB!
}
```

This function is responsible for finding subscribed channels when a group message (0x05) arrives. Since it always returns 0 (not found), **the firmware never decrypts or delivers channel messages to the companion radio**.

**Result:**
- Channel messages remain encrypted at the firmware level
- They're only visible as raw packets in the packet logging stream
- The companion radio never sees them decoded

## What You're Seeing

Your packet from the Public channel:
```
[DEBUG] Received message [logRxData]: 39 bytes
[DEBUG] Hex: 31e41500115183f31d842c26a6b3e079e55eb98476833d90d3f4fc3cb160abd8610c54d8de3236
```

Breakdown:
- `31` = SNR * 4 (SNR = 12.25 dB)
- `e4` = RSSI (-28 dBm)
- `150011518...` = Raw wire-format packet
  - `15` = Header (Route: TRANSPORT_DIRECT, Type: ADVERT, Ver: 0)
  - `0011` = Payload length (0x0011 = 17 bytes, or little-endian interpretation)
  - Rest = Encrypted/encoded message data

## What Needs to Happen

### Option 1: Firmware Fix (Recommended)

The firmware needs to implement `searchChannelsByHash()` to:
1. Maintain a list of subscribed channels with their secrets
2. Search this list when group messages arrive
3. Decrypt and return matching channels

### Option 2: Implement Wire Format Decoder

The logger could implement a full Packet wire-format decoder:
```typescript
// This would require:
- Parsing the packet header (route type, payload type, version)
- Extracting payload_len and path_len
- Decrypting the payload if it's encrypted (PAYLOAD_TYPE_GRP_TXT requires decryption)
- Parsing the decrypted content (timestamp, sender, text)
```

This is complex because it requires:
- Knowledge of all channel secrets that are subscribed
- AES-256 decryption capability
- Understanding the full packet structure for each payload type

### Option 3: Frontend Workaround

The logger could:
1. Detect `PAYLOAD_TYPE_GRP_TXT` (0x05) packets in logRxData
2. Display them as "Encrypted Group Messages"
3. Note that decryption requires knowledge of the channel secret

## Updated Code

**Changes Made:**

1. **index.ts**: Added subscription to Public channel
   ```typescript
   radio.setChannel(0, 'Public', Buffer.alloc(16, 0));
   radio.subscribeHashChannel('#aqua-test', 1);
   ```

2. **logRxData Display**: Enhanced to show packet interpretation
   - Decodes header byte to show Route Type, Payload Type, Version
   - Shows payload length
   - Identifies GRP_TXT packets with notification

## Testing

To verify channels are working end-to-end:

1. **Check if messages show up as channelMessage:**
   ```
   Look for lines like:
   📢 Channel Message #N:
       Channel: X
       Text: [message]
   ```
   If you see these, channel decoding is working.

2. **Check if messages show up as logRxData:**
   ```
   Look for lines like:
   📡 Raw RX Packet Log:
       ...Type: 5 (GRP_TXT)
   ```
   If you see Type 5, this is a group message that the firmware didn't decode.

3. **Firmware Debugging:**
   - Enable debug output on the firmware
   - Send a test message to the Public channel
   - Check if firmware calls `onGroupDataRecv()` or if it fails at `searchChannelsByHash()`

## Recommendation

**Short term:** The logger now displays more information about raw packets, making it easier to identify group messages.

**Long term:** The firmware needs to implement channel storage and discovery (`searchChannelsByHash()`) to make channel subscriptions work properly.

## Next Steps

1. Verify the firmware implements channel subscription persistence
2. If not, implement Option 1 (firmware fix) to store subscribed channels
3. Once firmware is fixed, the companion radio will automatically decode and deliver channel messages
4. They'll appear as `channelMessage` responses instead of raw `logRxData`
