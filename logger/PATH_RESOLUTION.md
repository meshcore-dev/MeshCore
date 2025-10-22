# Path Resolution for Public Channel Messages

## Overview

This document explains how to resolve the path that a Public channel message took to reach this radio.

## Implementation

A new function `extractPathFromWirePacket()` has been added to parse the wire packet structure and extract the routing path. The path is now displayed as hexadecimal bytes in the `logRxData` output.

## Packet Structure

MeshCore wire packets have the following structure:

```
[Header (1 byte)]
[Transport Codes (optional, 4 bytes if ROUTE_TYPE is 0x00 or 0x03)]
[Path Length (1 byte)]
[Path (0-32 bytes)]
[Payload (rest)]
```

### Header Bits
- **Bits 0-1**: Route Type
  - `0x00`: TRANSPORT_FLOOD
  - `0x01`: FLOOD  
  - `0x02`: DIRECT
  - `0x03`: TRANSPORT_DIRECT
- **Bits 2-5**: Payload Type (REQ, RESPONSE, TXT_MSG, ACK, ADVERT, GRP_TXT, etc.)
- **Bits 6-7**: Payload Version

### Route Type Determines Transport Codes

Only route types `0x00` (TRANSPORT_FLOOD) and `0x03` (TRANSPORT_DIRECT) include 4-byte transport codes. All others proceed directly to path length.

## Path Extraction Logic

The `extractPathFromWirePacket()` function:

1. Reads the header byte
2. Checks route type bits (0-1) to determine if transport codes follow
3. Skips transport codes if present (4 bytes)
4. Reads the path length (1 byte)
5. Validates path length (must be ≤ 32 bytes and fit in buffer)
6. Returns path as uppercase hexadecimal string

### Example Output

```
📡 Raw RX Packet Log:
   SNR: 8.25 dB
   RSSI: -95 dBm
   📦 Packet Header: 0x51
       Route: 1 (FLOOD)
       Type: 5 (GRP_TXT)
       Ver: 1
       Payload Length: 82
       Path: 1A2B3C4D5E
       ℹ️  This is a GROUP TEXT MESSAGE (typical for channels)
       Channel Hash Byte: 0x29
   Packet: 51821a2b3c4d5e... (87 bytes)
```

## Path Interpretation

The path is a sequence of 1-byte node hashes representing the relay chain:

- **Direct messages**: Path is typically empty or contains only the sender
- **Relayed messages**: Path contains the sender followed by each relay node
- **Path order**: Shows the hop-by-hop route the packet took to reach this receiver

Example: Path `1A2B3C` means the message came from node `0x1A` → relayed through `0x2B` → relayed through `0x3C` → received here

## Wire Format Flow

For a Public channel message coming through as `logRxData`:

1. **Input**: Raw packet bytes from the firmware
2. **Parsing**: `extractPathFromWirePacket()` decodes the structure
3. **Output**: Human-readable path as hex bytes in console

## Future Enhancement

When the firmware's `searchChannelsByHash()` function is implemented:

- Public channel messages will decrypt and appear as `channelMessage` type
- The channel message type may be extended to include the raw path bytes
- Path will be displayed directly in the "Channel Message" output block

## Technical Notes

- Path bytes are node identity hashes (not full public keys)
- Each hash is 1 byte (truncated from longer keys)
- Maximum path length is 32 hops
- Empty path (`""`) indicates a directly-received message
- Paths help diagnose mesh routing and signal quality across relay hops
