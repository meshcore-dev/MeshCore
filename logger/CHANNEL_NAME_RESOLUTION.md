# Channel Name Resolution

## Overview

The logger now:
1. **Resolves channel names** - Displays channel names instead of just indices
2. **Uses correct Public channel secret** - Now subscribes to the Public channel with the proper pre-shared key (PSK)

## Changes Made

### 1. Channel Storage in RadioClient

Added channel tracking to the `RadioClient` class:

```typescript
private channels: Map<number, { name: string; secret: Buffer }> = new Map();
```

When `setChannel()` is called, the channel information is stored locally:

```typescript
public setChannel(channelIndex: number, name: string, secret: Buffer): void {
  // Store channel info locally for name resolution
  this.channels.set(channelIndex, { name, secret: secret.subarray(0, 16) });
  // ... send command to radio ...
}
```

### 2. Channel Name Lookup

Added `getChannelName()` method to retrieve stored channel names:

```typescript
public getChannelName(channelIndex: number): string | undefined {
  return this.channels.get(channelIndex)?.name;
}
```

### 3. Enhanced Channel Message Display

Updated the `channelMessage` handler to show channel names:

**Before:**
```
📢 Channel Message #5:
   Channel: 1
   Text: aquarat-g2n: Test
```

**After:**
```
📢 Channel Message #5:
   Channel: 1 (#aqua-test)
   Text: aquarat-g2n: Test
```

### 4. Public Channel Configuration

Fixed the Public channel subscription by using the correct pre-shared key:

```typescript
// The Public channel PSK from the firmware defaults
// PSK: "izOH6cXN6mrJ5e26oRXNcg==" (Base64-encoded = 16 bytes)
const publicChannelPSK = Buffer.from('izOH6cXN6mrJ5e26oRXNcg==', 'base64');
radio.setChannel(0, 'Public', publicChannelPSK);
```

## Expected Behavior

### Before the Fix

**Public channel messages** appeared only as raw packets:
```
📡 Raw RX Packet Log:
   SNR: 12.00 dB
   RSSI: -55 dBm
   📦 Packet Header: 0x15
       Type: 5 (GRP_TXT)
       ℹ️  This is a GROUP TEXT MESSAGE
       Channel Hash Byte: 0x29
```

**Hashtag channel messages** showed generic channel index:
```
📢 Channel Message #5:
   Channel: 1
```

### After the Fix

**Public channel messages** now decode properly:
```
📢 Channel Message #6:
   Channel: 0 (Public)
   Time: 2025-10-22T16:15:30.000Z
   SNR: 12.0dB
   Text: aquarat-g2n: Hello Public
```

**Hashtag channel messages** show friendly names:
```
📢 Channel Message #5:
   Channel: 1 (#aqua-test)
   Time: 2025-10-22T16:08:20.000Z
   SNR: 13.0dB
   Text: aquarat-g2n: Test
```

## Channel Reference

### Built-in Channels

| Index | Name | PSK (Base64) | Use Case |
|-------|------|--------------|----------|
| 0 | Public | `izOH6cXN6mrJ5e26oRXNcg==` | Global public group chat |

### Custom Channels

You can add more channels by calling `subscribeHashChannel()` for hashtag channels:

```typescript
radio.subscribeHashChannel('#my-channel', 2);  // Channel index 2
radio.subscribeHashChannel('#another-channel', 3);  // Channel index 3
```

## Technical Details

### Pre-Shared Key (PSK) Encoding

The Public channel PSK is stored as Base64 in the firmware:
- **Base64:** `izOH6cXN6mrJ5e26oRXNcg==`
- **Hex:** `8b 73 87 e9 c5 cd ea 6a c9 e5 ed ba a1 15 cd 72`
- **Length:** 16 bytes (128-bit key for AES encryption)

When the radio receives a group message:
1. It checks if there's a subscribed channel matching the message's hash
2. Uses the channel's PSK to decrypt the message
3. Delivers it as a `channelMessage` response

### Channel Hash Calculation

For hashtag channels (using `subscribeHashChannel()`):
1. Compute SHA256 hash of the hashtag string (e.g., `"#aqua-test"`)
2. Truncate to 16 bytes as the channel secret
3. The firmware uses this to identify matching messages

## Troubleshooting

### Public channel messages still appear as raw packets

**Cause:** The firmware may not recognize the channel secret or may not have channel support enabled.

**Solution:**
1. Verify the firmware version supports group channels
2. Check if the Public channel PSK is correct in the firmware
3. Look at the `Mesh.cpp` `searchChannelsByHash()` function - it may be a stub

### Channel names show as numbers only

**Cause:** The channel wasn't subscribed via `setChannel()` or `subscribeHashChannel()`.

**Solution:**
- Ensure you call the subscription methods before starting message listening
- The channel information is only available if explicitly set

### Messages still appear in logRxData instead of channelMessage

**Cause:** The firmware's `searchChannelsByHash()` function isn't finding the channel.

**Possible solutions:**
1. Verify the channel secret (PSK) is correct
2. Check if the firmware implements channel storage (not a stub function)
3. Look at raw packet data to see the channel hash byte (`0x29` in your example)

## Code Examples

### Subscribing to Multiple Channels

```typescript
const radio = new RadioClient();
await radio.connect('/dev/ttyACM0');

// Subscribe to Public channel
const publicPSK = Buffer.from('izOH6cXN6mrJ5e26oRXNcg==', 'base64');
radio.setChannel(0, 'Public', publicPSK);

// Subscribe to hashtag channels
radio.subscribeHashChannel('#aqua-test', 1);
radio.subscribeHashChannel('#developers', 2);
radio.subscribeHashChannel('#announcements', 3);

// Now channel messages will display with names!
```

### Getting Channel Information Programmatically

```typescript
// Get the name of a channel
const channelName = radio.getChannelName(1);
if (channelName) {
  console.log(`Channel 1 is: ${channelName}`);
} else {
  console.log(`Channel 1 is not subscribed`);
}
```

## Future Enhancements

Potential improvements:

1. **Channel list command** - Query the radio for all subscribed channels
2. **Dynamic channel addition** - Add/remove channels during runtime without restart
3. **Channel persistence** - Save channel subscriptions to a config file
4. **Custom PSK support** - Allow users to define custom PSKs for private channels
5. **Channel switching** - UI to switch between channels and filter messages
