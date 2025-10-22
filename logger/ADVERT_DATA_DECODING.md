# Advert Data Decoding and Display

## Overview

When the firmware discovers a new contact via an advert (advertisement), it now displays detailed information including:
- Contact name
- GPS coordinates (latitude/longitude in degrees)
- Public key
- Signal quality and routing path

## How Adverts Work in MeshCore

### Advert Message Types

The firmware sends adverts through two different mechanisms:

#### 1. **Simple Advert** (PUSH_CODE_ADVERT / 0x80 with manual add enabled)
- **Payload**: 32 bytes (public key only)
- **Use case**: When manual add is enabled (auto-add disabled)
- **Limitation**: No name or GPS information
- **Action**: User must retrieve contact details separately or enable auto-add

#### 2. **Full Advert** (PUSH_CODE_NEW_ADVERT / 0x8A with auto-add enabled)
- **Payload**: Complete contact structure (72+ bytes)
- **Contents**:
  - Public key (32 bytes)
  - Type (1 byte)
  - Flags (1 byte)
  - Out-path length (1 byte)
  - Out-path (32 bytes)
  - Name (32 bytes, null-terminated)
  - Last advert timestamp (4 bytes)
  - GPS latitude (4 bytes, int32)
  - GPS longitude (4 bytes, int32)
  - Last modified (4 bytes)
- **Use case**: Default behavior when auto-add is enabled
- **Advantage**: Contains all contact info including name and GPS

### Advert Data Format

When an advert includes GPS and name, they're encoded in a special advert data structure:

```
Byte 0: Flags/Type
  ├─ Bits 0-3: Type (0=NONE, 1=CHAT, 2=REPEATER, 3=ROOM, 4=SENSOR)
  ├─ Bit 4: ADV_LATLON_MASK (1 if latitude/longitude present)
  ├─ Bit 5: ADV_FEAT1_MASK (reserved)
  ├─ Bit 6: ADV_FEAT2_MASK (reserved)
  └─ Bit 7: ADV_NAME_MASK (1 if name present)

If bit 4 set: Latitude (4 bytes int32, degrees * 1e6)
If bit 4 set: Longitude (4 bytes int32, degrees * 1e6)
If bit 5 set: Feature 1 (2 bytes)
If bit 6 set: Feature 2 (2 bytes)
If bit 7 set: Name (remaining bytes, null-terminated)
```

### GPS Coordinate Storage

GPS coordinates are stored as **32-bit integers representing degrees × 1e6**:

| Stored Value | Actual Degrees | Example |
|---|---|---|
| 37500000 | 37.5° | 37.5° North |
| -122500000 | -122.5° | 122.5° West |

**Conversion**: `degrees = value / 1e6`

## Current Implementation

### Data Structures

**Contact Interface** (`types.ts`):
```typescript
interface Contact {
  id: { pubKey: Buffer };
  name: string;
  type: number;
  flags: number;
  outPathLen: number;
  outPath: Buffer;
  lastAdvertTimestamp: number;
  lastModified: number;
  gpsLat?: number;  // int32, degrees * 1e6
  gpsLon?: number;  // int32, degrees * 1e6
}
```

**ParsedAdvertData Interface** (`messageDecoder.ts`):
```typescript
interface ParsedAdvertData {
  type: number;
  name?: string;
  latitude?: number;   // converted to degrees
  longitude?: number;  // converted to degrees
  hasLocation: boolean;
}
```

### Message Handlers

#### Full Advert Handler
When `contact` type is received:
```
📍 Contact Advert:
   Name: aquarat-g2n
   Public Key: 0ff0b500183e5a0048a113fd97d78631cf58378f9a892585aec1e39aec6dd574
   Location: 37.500000, -122.500000
```

#### Simple Advert Handler
When `simpleAdvert` type is received:
```
📍 Simple Advert (pubkey only):
   Public Key: 0ff0b500183e5a0048a113fd97d78631cf58378f9a892585aec1e39aec6dd574
   (To get name and GPS, enable auto-add in radio settings)
```

## Retrieving Full Advert Data

### Option 1: Enable Auto-Add (Recommended)

By default, MeshCore is configured to **automatically add** new contacts. When auto-add is enabled:
- New adverts trigger `PUSH_CODE_NEW_ADVERT` (0x8A)
- Full contact info is sent including name and GPS
- You immediately see complete contact information

### Option 2: Manual Add Mode

If you disable auto-add to manually approve contacts:
- New adverts trigger `PUSH_CODE_ADVERT` (0x80)
- Only 32-byte public key is sent initially
- To get full details, you must:
  1. Accept the contact (adds it to your contacts list)
  2. Or query the contacts list with `CMD_GET_CONTACTS`

### Option 3: Request Advert Path

Use `CMD_GET_ADVERT_PATH` to retrieve the path information for a contact:
- Returns timestamp and routing path
- Does NOT return name or GPS coordinates
- Used for network topology discovery

## Code Implementation

### Parse Advert Data Function

```typescript
function parseAdvertData(advertData: Buffer): ParsedAdvertData | null {
  // Reads flags byte to determine what data is present
  // Extracts name if ADV_NAME_MASK set (bit 7)
  // Extracts/converts GPS if ADV_LATLON_MASK set (bit 4)
  // Returns parsed structure with all available fields
}
```

**Input**: Buffer containing encoded advert data
**Output**: ParsedAdvertData with name, latitude, longitude, or null if invalid

### Display Contact Info Function

```typescript
function displayContactInfo(contact: any, title: string = '📍 Contact Advert'): void {
  // Displays name if available
  // Displays public key (hex format)
  // Displays GPS coordinates if available (6 decimal places)
  // Formats output for console
}
```

**Used for**: Both full contacts and parsed advert data

## Example Workflow

### When Contact is Auto-Added

1. **Receive advert packet** in mesh
2. **Firmware receives and parses** advert (decrypts if needed)
3. **Firmware extracts** name, GPS, public key from advert data
4. **Firmware creates ContactInfo** struct with all fields
5. **Firmware sends PUSH_CODE_NEW_ADVERT** (0x8A) with full contact
6. **Logger receives** contact message type
7. **Logger displays** formatted contact info with name and GPS

### When Manual Add is Enabled

1. **Receive advert packet** in mesh
2. **Firmware receives and parses** advert
3. **Firmware sends PUSH_CODE_ADVERT** (0x80) with 32-byte pubkey only
4. **Logger receives** simpleAdvert message type
5. **Logger displays** pubkey with hint to enable auto-add for full info
6. **User enables auto-add** in radio settings
7. **Next advert from same node** triggers full contact send

## Troubleshooting

### Issue: "Simple Advert" Shown Instead of Full Contact

**Cause**: Manual add is enabled on the radio (auto-add is disabled)

**Solution**:
- Check radio auto-add setting
- Send a new advert from the remote device
- Contact will then appear with full information

### Issue: GPS Shows as (0, 0)

**Possible causes**:
1. Remote device doesn't have GPS enabled
2. Remote device hasn't acquired GPS lock yet
3. Remote device GPS data not initialized

**Check**:
- Verify remote has GPS hardware
- Wait for GPS to acquire satellites (takes 30-60 seconds initially)
- Verify GPS is enabled in device settings

### Issue: Name is Empty/Blank

**Causes**:
1. Remote device name not set
2. Advert data format issue

**Solution**:
- Set device name on remote radio
- Send new advert

### Issue: Cannot Parse Coordinates

**Debug**:
- Check raw GPS values (multiply by 1e6 to get stored integer)
- Verify latitude is -90 to +90
- Verify longitude is -180 to +180

## Future Enhancements

### Planned Features

1. **Altitude Support**: Firmware includes 4-byte altitude field (reserved)
2. **Custom Fields**: ADV_FEAT1_MASK and ADV_FEAT2_MASK for future use
3. **Contact Update Events**: Separate event when GPS changes
4. **Map Integration**: Display contacts on map with coordinates
5. **Contact Search**: Find contacts by name or location

### Firmware Improvements Needed

1. **Implement searchChannelsByHash()**: Would enable automatic channel decryption
2. **Altitude Field**: Add altitude to advert data when available
3. **Contact Expiry**: Mark old contacts as stale
4. **Better GPS Accuracy**: Include GPS accuracy/dilution of precision

## Performance Considerations

- **Simple Advert**: 32 bytes → minimal bandwidth
- **Full Advert**: 72+ bytes → slightly higher bandwidth, much more useful
- **GPS Updates**: Each new advert updates timestamps
- **Name Caching**: Logger caches contact names locally in radioClient
- **Display Performance**: No performance impact from parsing (< 1ms per advert)

## References

- **Firmware**: `/home/aquarat/Projects/MeshCore/src/helpers/AdvertDataHelpers.cpp`
- **Contact Structure**: `/home/aquarat/Projects/MeshCore/src/helpers/ContactInfo.h`
- **Firmware Advert Handling**: `/home/aquarat/Projects/MeshCore/examples/companion_radio/MyMesh.cpp`
- **Chat Example**: `/home/aquarat/Projects/MeshCore/src/helpers/BaseChatMesh.cpp`
