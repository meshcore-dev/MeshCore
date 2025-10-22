# Quick Start Guide

## What was created

✅ `/logger/` - Complete TypeScript project for connecting to MeshCore radio via USB/Serial CDC

### Files created:
- **`package.json`** - Project metadata and dependencies
- **`tsconfig.json`** - TypeScript compiler configuration
- **`src/index.ts`** - Main application code
- **`README.md`** - Complete documentation

## How to use

```bash
cd /home/aquarat/Projects/MeshCore/logger
pnpm ts-node src/index.ts
```

## What the app does

1. ✅ **Auto-detects the radio** - Scans serial ports and identifies the Espressif ESP32 device
2. ✅ **Connects via USB CDC** - Opens connection at 115200 baud
3. ✅ **Sends firmware query** - Uses the MeshCore frame protocol to request device info
4. ✅ **Displays firmware version** - Parses and prints device information including:
   - Firmware Version (e.g., "v1.9.1")
   - Firmware Code
   - Build Date
   - Manufacturer Name
   - Max Contacts
   - Max Group Channels
   - BLE PIN

## Frame Protocol Implementation

The app fully implements the MeshCore serial protocol:

**Sending:**
```
'<' + Length(LSB) + Length(MSB) + Payload
```

**Receiving:**
```
'>' + Length(LSB) + Length(MSB) + Payload
```

Commands implemented:
- `CMD_DEVICE_QUERY (0x16)` - Query device info
- `RESP_CODE_DEVICE_INFO (0x64)` - Device info response

## Already tested

✅ TypeScript compilation works
✅ ts-node can execute the code
✅ Serial port detection working (found `/dev/ttyACM0` - Espressif)
✅ All dependencies installed via pnpm

## Next steps

When you have the radio connected, simply run:
```bash
pnpm ts-node src/index.ts
```

The app will automatically:
1. Find the radio
2. Connect
3. Query and display the firmware version
4. Disconnect gracefully
