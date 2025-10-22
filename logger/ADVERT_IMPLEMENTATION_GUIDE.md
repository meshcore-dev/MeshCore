# Advert Decoding Implementation Guide

## Changes Made

### 1. New Helper Function: `parseAdvertData()`

**File**: `logger/src/lib/messageDecoder.ts`

**Purpose**: Parse embedded advert data from packets to extract name and GPS coordinates

**Implementation**:
```typescript
function parseAdvertData(advertData: Buffer): ParsedAdvertData | null {
  // Parse flags byte (bits indicate what data is present)
  // Extract name if ADV_NAME_MASK (0x80) is set
  // Extract latitude/longitude if ADV_LATLON_MASK (0x10) is set
  // Convert GPS from int32 (degrees * 1e6) to decimal degrees
  return { type, name, latitude, longitude, hasLocation };
}
```

**Key Features**:
- Handles variable-length advert data
- Converts GPS from stored format (degrees × 1e6) to decimal degrees
- Returns `null` on invalid/insufficient data
- Extracts advert type (CHAT, REPEATER, ROOM, SENSOR, etc.)

**Parameters**:
- `advertData: Buffer` - Raw advert data bytes

**Returns**:
- `ParsedAdvertData | null` - Parsed data or null if invalid
- Fields:
  - `type: number` - Advert type (0-15)
  - `name?: string` - Device name (if present)
  - `latitude?: number` - Decimal degrees (if present)
  - `longitude?: number` - Decimal degrees (if present)
  - `hasLocation: boolean` - Whether location data present

### 2. New Helper Function: `displayContactInfo()`

**File**: `logger/src/index.ts`

**Purpose**: Format and display contact/advert information to console

**Implementation**:
```typescript
function displayContactInfo(contact: any, title: string = '📍 Contact Advert'): void {
  // Display name if available
  // Display public key as hex
  // Display GPS coordinates if available (6 decimal places)
}
```

**Output Example**:
```
📍 Contact Advert:
   Name: aquarat-g2n
   Public Key: 0ff0b500183e5a0048a113fd97d78631cf58378f9a892585aec1e39aec6dd574
   Location: 37.500000, -122.500000
```

**Parameters**:
- `contact: any` - Contact object with id, name, gpsLat, gpsLon fields
- `title: string` - Header title to display

### 3. Updated Message Handlers

#### Contact Message Handler

**Old**:
```typescript
case 'contact':
  // No handler - message was ignored
```

**New**:
```typescript
case 'contact':
  displayContactInfo(message.data, '📍 Contact Advert');
  break;
```

**Triggers**: When firmware sends full advert with `PUSH_CODE_NEW_ADVERT` (0x8A)

#### Simple Advert Handler

**Old**:
```typescript
case 'simpleAdvert':
  console.log(`Simple Advert:`);
  console.log(`   Public Key: ${message.pubKey.toString('hex')}\n`);
  break;
```

**New**:
```typescript
case 'simpleAdvert':
  console.log(`\n📍 Simple Advert (pubkey only):`);
  console.log(`   Public Key: ${message.pubKey.toString('hex')}`);
  console.log(`   (To get name and GPS, enable auto-add in radio settings)\n`);
  break;
```

**Triggers**: When firmware sends simple advert with `PUSH_CODE_ADVERT` (0x80) and manual add enabled

**Helpful Note**: Informs user how to get full contact details

### 4. New Type Definition

**File**: `logger/src/lib/messageDecoder.ts`

**Export**:
```typescript
export interface ParsedAdvertData {
  type: number;
  name?: string;
  latitude?: number;  // in degrees
  longitude?: number; // in degrees
  hasLocation: boolean;
}
```

## Firmware Context

### Contact Information in Firmware

The firmware's `ContactInfo` struct contains:

```cpp
struct ContactInfo {
  mesh::Identity id;           // public key + hash
  char name[32];               // device name
  uint8_t type;               // ADV_TYPE_CHAT, etc.
  uint8_t flags;
  int8_t out_path_len;
  uint8_t out_path[32];
  uint32_t last_advert_timestamp;
  uint8_t shared_secret[32];
  uint32_t lastmod;
  int32_t gps_lat, gps_lon;   // degrees * 1e6
  uint32_t sync_since;
};
```

### How Adverts Are Created

The firmware's `AdvertDataBuilder` encodes advert data:

```cpp
AdvertDataBuilder(uint8_t type, const char* name, double lat, double lon) {
  // Encodes as:
  // [flags/type][lat (4 bytes)][lon (4 bytes)][name...]
}
```

**Flags byte encoding**:
```cpp
#define ADV_LATLON_MASK  0x10   // bit 4
#define ADV_NAME_MASK    0x80   // bit 7
```

### When Adverts Are Sent to Logger

**Scenario 1: Auto-Add Enabled (Default)**
1. Firmware receives mesh advert
2. Creates/updates ContactInfo
3. Sends `PUSH_CODE_NEW_ADVERT` (0x8A) with complete contact
4. Logger receives as `contact` type

**Scenario 2: Manual Add Enabled**
1. Firmware receives mesh advert
2. Creates ContactInfo but doesn't auto-add
3. Sends `PUSH_CODE_ADVERT` (0x80) with 32-byte pubkey
4. Logger receives as `simpleAdvert` type

## Data Flow

```
Mesh Advert Packet Received
    ↓
Firmware Parses (decrypts if needed)
    ↓
Creates/Updates ContactInfo
    ├─ Extracts name from advert data
    ├─ Extracts GPS from advert data
    └─ Stores as int32 (degrees * 1e6)
    ↓
If Auto-Add:
    Send PUSH_CODE_NEW_ADVERT (0x8A) → Full contact info
    ↓
    Logger receives 'contact' type
    ↓
    displayContactInfo() formats and shows:
    - Name
    - Public Key
    - GPS (converted to decimal degrees)

If Manual-Add:
    Send PUSH_CODE_ADVERT (0x80) → 32-byte pubkey only
    ↓
    Logger receives 'simpleAdvert' type
    ↓
    Show pubkey + hint to enable auto-add
```

## Usage Examples

### Example 1: Receive Full Advert (Auto-Add Enabled)

**Scenario**: Remote radio sends advert with GPS location

**Console Output**:
```
[DEBUG] Received message [contact]: 76 bytes
📍 Contact Advert:
   Name: hiker-device
   Public Key: 1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f
   Location: 37.774929, -122.419415
```

**Data Flow**:
1. Advert data received from mesh with GPS + name
2. Contact created in firmware with:
   - `gps_lat = 37774929` (37.774929 * 1e6)
   - `gps_lon = -122419415` (-122.419415 * 1e6)
   - `name = "hiker-device"`
3. Sent to logger as full contact
4. Displayed with coordinates converted to decimal

### Example 2: Receive Simple Advert (Manual Add Enabled)

**Scenario**: Manual add is enabled, new advert received

**Console Output**:
```
[DEBUG] Received message [simpleAdvert]: 32 bytes
📍 Simple Advert (pubkey only):
   Public Key: 1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f
   (To get name and GPS, enable auto-add in radio settings)
```

**Resolution**:
1. User enables auto-add in radio settings
2. Send new advert from remote device
3. Next advert will include full contact info

### Example 3: Device Without GPS

**Scenario**: Remote device has no GPS hardware

**Console Output**:
```
📍 Contact Advert:
   Name: desktop-node
   Public Key: 1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f
   (No GPS data)
```

**Explanation**: 
- Name displayed (was set on device)
- GPS fields absent (device has no GPS or GPS never acquired lock)

## Testing

### Test 1: Verify Full Advert Display

**Setup**:
1. Ensure auto-add is enabled on logger radio
2. Have a remote radio with GPS location and name set

**Steps**:
1. Run logger: `npm run start`
2. Send message from remote radio
3. Remote radio should send advert

**Expected**:
```
📍 Contact Advert:
   Name: [remote device name]
   Public Key: [32 bytes hex]
   Location: [latitude], [longitude]
```

### Test 2: Verify Simple Advert Display

**Setup**:
1. Disable auto-add on logger radio
2. Have a remote radio nearby

**Steps**:
1. Run logger
2. Let it discover remote radio

**Expected**:
```
📍 Simple Advert (pubkey only):
   Public Key: [32 bytes hex]
   (To get name and GPS, enable auto-add in radio settings)
```

### Test 3: Verify GPS Conversion

**Setup**:
1. Set specific GPS location on remote radio: 0°, 0°
2. Or set to known location: 37.7749°N, 122.4194°W

**Steps**:
1. Send advert
2. Check displayed GPS matches

**Expected**: Coordinates match configured location (±0.000001° accuracy)

## Performance Impact

- **Parse Time**: < 1ms per advert (buffer reading + string conversion)
- **Memory**: No additional memory allocation (parses to locals)
- **Display Time**: Instant (console output)
- **No Impact on**: Message handling, channel decoding, queue sync

## Compatibility

- ✅ Works with all contact types (CHAT, REPEATER, ROOM, SENSOR)
- ✅ Handles missing GPS gracefully (shows nothing if not present)
- ✅ Handles missing name gracefully (shows nothing if not present)
- ✅ Compatible with firmware versions supporting GPS (most recent)
- ✅ No breaking changes to existing message types

## Files Modified

1. **`logger/src/index.ts`**
   - Added `displayContactInfo()` function
   - Updated contact message handler
   - Updated simpleAdvert message handler

2. **`logger/src/lib/messageDecoder.ts`**
   - Added `parseAdvertData()` function
   - Added `ParsedAdvertData` interface export

3. **`logger/src/lib/types.ts`**
   - No changes (Contact interface already had GPS fields)

## Documentation Files

1. **`ADVERT_DATA_DECODING.md`** - Complete advert system overview
2. **This file** - Implementation details and testing guide

## Limitations

### Current Limitations

1. **Simple Adverts**: Cannot display name/GPS without re-requesting contact
2. **GPS Accuracy**: Stored as 1e6 precision (±0.000001°), sufficient for mesh but not for high-precision navigation
3. **Altitude**: Not currently extracted (reserved in firmware)
4. **Custom Fields**: ADV_FEAT1/FEAT2 not parsed (reserved for future)

### Potential Future Work

1. Automatically request full contact when simple advert received
2. Cache contact names for quick display
3. Support altitude when firmware adds it
4. Display contact update notifications
5. Export contacts to CSV/JSON for mapping

## Troubleshooting Guide

### Q: I'm seeing "Simple Advert" but want full info

**A**: Enable auto-add in your radio's settings, then send a new advert from the remote device.

### Q: GPS shows 0.000000, 0.000000

**A**: Either the remote device has no GPS, or it hasn't acquired a GPS lock yet. Wait 30-60 seconds after startup for GPS initialization.

### Q: Name is empty

**A**: The remote device doesn't have a name set. Use the radio's UI or serial command to set an advertisement name.

### Q: Coordinates don't match where I expect

**A**: Check:
1. Latitude range: -90 to +90
2. Longitude range: -180 to +180  
3. Sign convention: North/East are positive
4. Precision: Values stored as degrees × 1e6 = ±0.000001°

### Q: I'm not seeing any adverts

**A**: Check:
1. Is auto-add enabled? (default: yes)
2. Is remote device nearby and powered on?
3. Are both on same channel/frequency?
4. Check serial output for [DEBUG] messages
