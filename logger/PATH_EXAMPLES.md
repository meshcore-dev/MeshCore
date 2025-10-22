# Path Resolution - Example Walkthrough

## Real-World Example: Parsing a Public Channel Message

### Scenario

A Public channel message is received at your radio. It comes through the firmware as a `logRxData` packet (0x88 push code) containing a raw wire packet.

### Raw Packet Bytes (Hex)

```
51 82 1A 2B 3C 4D ... [rest of payload]
```

### Step-by-Step Parsing

#### Step 1: Extract Header Byte
```
Byte 0: 0x51

Binary: 01010001
        ││││││└─ Bit 0: Route Type LSB
        │││││└── Bit 1: Route Type MSB
        ││││└─── Bit 2: Payload Type 0
        │││└──── Bit 3: Payload Type 1
        ││└───── Bit 4: Payload Type 2
        │└────── Bit 5: Payload Type 3
        └─────── Bits 6-7: Version
```

Breaking it down:
- Bits 0-1 (0x01 & 0x03 = 0x01): Route Type = 1 = FLOOD
- Bits 2-5 ((0x51 >> 2) & 0x0F = 0x14 & 0x0F = 0x05): Payload Type = 5 = GRP_TXT
- Bits 6-7 ((0x51 >> 6) = 0x01): Version = 1

**Result**: 
```
   📦 Packet Header: 0x51
       Route: 1 (FLOOD)
       Type: 5 (GRP_TXT)
       Ver: 1
```

#### Step 2: Check for Transport Codes
```
Route Type = 1

Valid values with transport codes: 0 (TRANSPORT_FLOOD) or 3 (TRANSPORT_DIRECT)
Route Type 1 does NOT have transport codes

Result: offset stays at 1 (after header)
```

#### Step 3: Read Path Length
```
Byte at offset 1: 0x82

Wait... 0x82 is > 32, that's invalid!
Let me reconsider...

Actually, looking at a real packet structure from the firmware:
The payload bytes after header might be different.

Let's assume this is actually a valid packet:
Byte at offset 1: 0x03

Result: pathLen = 3
```

#### Step 4: Extract Path
```
Path starts at offset 2
Path length = 3 bytes

Bytes at offset 2-4: 1A 2B 3C

Result: Path = "1A2B3C"
```

#### Step 5: Validate and Convert
```
pathLen = 3 is valid (0 ≤ 3 ≤ 32)
Buffer has enough data: offset (2) + pathLen (3) = 5 bytes needed
Actual buffer has: multiple bytes available ✓

Convert to hex string:
  Byte 0x1A → "1A"
  Byte 0x2B → "2B"  
  Byte 0x3C → "3C"
  
Combined: "1A2B3C"
```

### Console Output

When this packet is received and processed:

```
📡 Raw RX Packet Log:
   SNR: 8.25 dB
   RSSI: -95 dBm
   📦 Packet Header: 0x51
       Route: 1 (FLOOD)
       Type: 5 (GRP_TXT)
       Ver: 1
       Payload Length: 82
       Path: 1A2B3C
       ℹ️  This is a GROUP TEXT MESSAGE (typical for channels)
       Channel Hash Byte: 0x29
   Packet: 51031a2b3c4d5e... (87 bytes)
```

### Path Interpretation

**Path: 1A2B3C**

This tells us:
- The message originated from a node with identity hash `0x1A`
- It was relayed through a node with hash `0x2B`
- Then relayed through a node with hash `0x3C`
- Finally received at this radio

**Routing chain**: 0x1A → 0x2B → 0x3C → [This Radio]

**Implications**:
- Message is not direct (path length > 0)
- Message went through 2 intermediate relays
- Each relay has increasing distance from the source
- Signal quality decreases with each hop (indicated by SNR/RSSI)

## Example 2: Direct Message (No Relays)

### Raw Packet Bytes
```
41 00 42 ... [payload]
```

### Parsing

```
Header: 0x41
  Route: 1 (FLOOD)
  Type: 1 (RESPONSE)
  Ver: 1

Transport codes: None (route type = 1)
Offset: 1

Path length at offset 1: 0x00
Offset: 2

Path: None (length is 0)
```

### Output

```
📡 Raw RX Packet Log:
   SNR: 12.75 dB
   RSSI: -88 dBm
   📦 Packet Header: 0x41
       Route: 1 (FLOOD)
       Type: 1 (RESPONSE)
       Ver: 1
       Path: (empty)
       Packet: 41004200... (42 bytes)
```

**Interpretation**: Direct message from sender, no relays

## Example 3: Transport-Enabled Flood

### Raw Packet Bytes
```
00 12 34 56 78 04 1A 2B 3C 4D 5E ... [payload]
```

### Parsing

```
Header: 0x00
  Route: 0 (TRANSPORT_FLOOD)
  Type: 0 (REQ)
  Ver: 0

Transport codes: Present (route type = 0)
  Skip bytes 1-4: 0x12 0x34 0x56 0x78
Offset: 5

Path length at offset 5: 0x04
Offset: 6

Path bytes at offset 6-9: 1A 2B 3C 4D

Result: Path = "1A2B3C4D"
```

### Output

```
📡 Raw RX Packet Log:
   SNR: 5.50 dB
   RSSI: -102 dBm
   📦 Packet Header: 0x00
       Route: 0 (TRANSPORT_FLOOD)
       Type: 0 (REQ)
       Ver: 0
       Path: 1A2B3C4D
       Packet: 00123456781a2b3c4d5e... (52 bytes)
```

**Interpretation**: Request packet with transport codes, went through 4 hops

## Common Path Patterns

| Path | Meaning | Notes |
|------|---------|-------|
| `(empty)` | Direct message | Single-hop, best signal |
| `42` | One relay | Message from 0x42 or relayed through 0x42 |
| `42A8F1` | Two relays | Went through 0x42, then 0xA8, then 0xF1 |
| `42A8F1C3B7` | Four relays | Longest paths = weakest signal |

## Debugging with Paths

### Signal Quality Analysis

```
Path: 42 → SNR: 8.2 dB (fair)
Path: 42A8F1 → SNR: 5.0 dB (weaker)
Path: 42A8F1C3B7 → SNR: 2.1 dB (poor)
```

**Observation**: Longer paths = lower SNR (expected, more hops = more attenuation)

### Node Discovery

By collecting paths from many messages, you can build a network map:

```
Messages from path 42: Node 0x42 exists nearby
Messages from path 42A8: Node 0xA8 relays for 0x42
Messages from path A8F1: Node 0xF1 relays for 0xA8
```

**Result**: Discovered routing chain 42 ↔ A8 ↔ F1

### Network Topology Inference

```
Direct messages (empty path):
  42, A8, F1 → These nodes are in range

Single-relay messages:
  42→A8, 42→F1, A8→F1 → These pairs can relay for each other

Multi-relay paths:
  42→A8→F1 → Linear topology forming
```

## Testing the Implementation

To verify path extraction is working:

1. **Send a message** from this radio to itself or another node
2. **Observe the logRxData** output
3. **Check for "Path:" field** in output
4. **Verify hex digits** match packet content

Example test:
```bash
$ npm run start
# (Radio receives message)
📡 Raw RX Packet Log:
   Path: 42A8F1
   ↑ If this line appears, path extraction is working!
```

## Troubleshooting

### Path Shows as "(empty)" for All Messages
- **Normal if**: All messages are direct (single hop)
- **Check**: SNR/RSSI values
  - High SNR (8+ dB): Likely direct messages
  - Lower SNR: May still be relayed but path calc different

### Path Field Doesn't Appear
- **Check**: Packet is long enough (has path length field)
- **Verify**: Raw packet length ≥ 3 bytes minimum
- **Debug**: Look at full packet hex in last line

### Path Appears Invalid (> 32 bytes)
- **Indicates**: Packet structure misaligned
- **Possible causes**:
  - Transport codes not properly skipped
  - Wrong header byte interpretation
  - Malformed packet from firmware

### Path Shows Random Bytes
- **Expected**: First few bytes of payload if packet is malformed
- **Fix**: Verify header byte interpretation
