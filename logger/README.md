# MeshCore Radio Logger

A TypeScript application that connects to a MeshCore radio via USB/Serial CDC and queries the device firmware version.

## Prerequisites

- Node.js 18+ with pnpm
- A MeshCore-compatible radio (e.g., T-Beam Supreme) connected via USB

## Setup

Install dependencies:

```bash
pnpm install
```

## Usage

Query the connected radio for firmware information:

```bash
pnpm ts-node src/index.ts
```

Or run with a specific serial port:

```bash
pnpm ts-node src/index.ts /dev/ttyACM0
```

## What It Does

1. **Scans for serial ports** - Automatically detects available serial ports and identifies the radio
2. **Connects** - Opens a connection at 115200 baud (standard for MeshCore devices)
3. **Sends query** - Sends a `CMD_DEVICE_QUERY` frame using the MeshCore frame protocol
4. **Parses response** - Receives the `RESP_CODE_DEVICE_INFO` frame and extracts:
   - Firmware Version
   - Firmware Code
   - Build Date
   - Manufacturer Name
   - Max Contacts
   - Max Group Channels
   - BLE PIN

## Protocol Details

The app implements the MeshCore serial frame protocol:

### Outgoing Frame Format
```
['>'][Length LSB][Length MSB][Payload...]
```

### Incoming Frame Format
```
['<'][Length LSB][Length MSB][Payload...]
```

### Frame Payload
- **CMD_DEVICE_QUERY**: `[0x16][protocol_version]`
- **RESP_CODE_DEVICE_INFO**: `[0x64][fw_code][max_contacts][max_channels][ble_pin_4bytes][build_date_12bytes][manufacturer_40bytes][firmware_version_20bytes]`

## Example Output

```
Available serial ports:
  [0] /dev/ttyACM0 - Espressif

Using port: /dev/ttyACM0
Connected to /dev/ttyACM0 at 115200 baud
Querying device info...

=== Device Information ===
Firmware Version: v1.9.1
Firmware Code: 7
Build Date: 2 Oct 2025
Manufacturer: LilyGo T-Beam
Max Contacts: 300
Max Group Channels: 8
BLE PIN: 123456
=========================

Disconnected
```

## Development

To modify or extend the application:

1. Edit `src/index.ts`
2. Test with `pnpm ts-node src/index.ts`
3. Build TypeScript: `pnpm exec tsc`

## Dependencies

- **serialport**: ^12.0.0 - Serial port communication
- **typescript**: ^5.0.0 - TypeScript compiler
- **ts-node**: ^10.0.0 - Run TypeScript directly
- **@types/node**: ^20.0.0 - Node.js type definitions

## License

Same as MeshCore project
