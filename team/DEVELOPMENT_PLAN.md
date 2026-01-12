# MeshCore Hunting/Tactical Android App - Development Plan

**Project Goal**: Create an Android app for position tracking and messaging over MeshCore LoRa mesh network for hunting and tactical use when other networks are unavailable.

**Last Updated**: January 11, 2026

---

## Current Progress

**Phase**: Location Tracking & Maps (Phase 3)
**Status**: ✅ Core Features Complete - Map & Telemetry Operational

**Completed**:
- ✅ **Phase 1 - BLE Foundation**: Scanner, connection manager, command serialization, response parsing, connection UI, permissions
- ✅ **Phase 2 - Database Layer**: Message/Node/Channel/AckRecord entities, DAOs, Room database, repositories  
- ✅ **Phase 2 - Messaging UI**: MessageScreen with channel selector, message bubbles, delivery status indicators, input field
- ✅ **Navigation**: Three-screen navigation (messages ↔ map ↔ connection) with bottom navigation bar
- ✅ **Mock Data Support**: Test messaging without hardware
- ✅ **Dark Theme**: Default dark theme enabled
- ✅ **BLE Message Sending**: Integrated with CommandSerializer, encrypted pairing support
- ✅ **Message Polling**: Automatic 2-second polling loop (CMD_SYNC_NEXT_MESSAGE)
- ✅ **Bidirectional Messaging**: Send and receive working with companion radio
- ✅ **Real-time UI Updates**: Fixed Compose recomposition for instant status updates
- ✅ **MTU Negotiation**: Request 185-byte MTU for full message frames
- ✅ **Message Format Parsing**: Parse "SENDERID: MESSAGE" format from MeshCore firmware
- ✅ **Delivery Status**: Checkmark appears immediately when companion confirms transmission
- ✅ **ACK Tracking**: PUSH_CODE_SEND_CONFIRMED handling, checksum calculation, delivery count display
- ✅ **Channel Management**: Private channel creation, deletion, public channel, channel selection UI
- ✅ **Channel Key Sharing**: QR code generation and manual link sharing for private channel invites
- ✅ **QR Code Scanner**: Manual entry with validation for meshcore:// URLs
- ✅ **Device Settings**: Rename companion radio via CMD_SET_ADVERT_NAME on connection screen
- ✅ **Location Preferences**: User-selectable location source (Phone GPS vs Companion GPS) with DataStore persistence
- ✅ **Map Integration**: OpenStreetMap (osmdroid) with real-time location display and mesh node markers
- ✅ **Telemetry System**: Hidden location tracking messages (#TEL:) in private channels with hop count updates
- ✅ **Background Telemetry**: Automatic location sharing via WorkManager with configurable channel and enable/disable toggle
- ✅ **Contact Updates**: Automatic node database updates from telemetry (lat/lon, battery, hops, last seen)
- ✅ **Connectivity Status**: Color-coded node markers (Direct/Relayed/Distant/Offline) based on hop count and last seen time
- ✅ **Map Legend**: Dropdown info button showing network status color legend

**Recent Work (Jan 11, 2026)**:
- Implemented full map screen with OpenStreetMap integration
- Added MapViewModel with FusedLocationProviderClient for phone GPS
- Created location source selection UI (phone vs companion GPS)
- Built telemetry tracking settings UI on connection screen
- Implemented TelemetryManager for automatic background location sharing
- Integrated telemetry with WorkManager for periodic updates (15 min intervals)
- Connected MapViewModel location updates to immediate telemetry sends
- Added telemetry message parsing and hidden message filtering (not shown in chat)
- Implemented node database updates from incoming telemetry (location, battery, hops)
- Created connectivity color system (green/yellow/orange/red) for map markers
- Added collapsible network status legend to map top bar
- Moved companion settings (device name, location source, telemetry) to connection page

**Known Limitations**:
- ⚠️ **Channel Message ACK Tracking**: Current companion radio firmware doesn't calculate/forward ACKs for group channel messages, only for direct person-to-person messages. This means "Delivered (X)" count only works for direct messages, not public/private channels. Requires firmware enhancement to `examples/companion_radio/MyMesh.cpp` to calculate ACK checksums for channel messages and store them in `expected_ack_table`.
- ⚠️ **Companion GPS**: Location source can be set to "companion" but GPS parsing from companion radio is not yet implemented (placeholder in MapViewModel)
- ⚠️ **QR Code Decoder**: QR scanner detects patterns but doesn't decode codes - manual entry works perfectly as fallback

**Next Tasks**:
- [ ] Implement companion GPS parsing from telemetry or dedicated command
- [ ] Add contact syncing from companion radio (CMD_GET_CONTACTS) 
- [ ] Add direct messaging to contacts with recipient selection
- [ ] Test ACK tracking with direct messages
- [ ] Add sender identification (extract device info from advertisements to replace placeholder keys)
- [ ] Fix QR code decoder or verify codes generated from TEAM app work
- [ ] Add custom map marker icons (different for repeaters, room servers, etc.)
- [ ] Add connection lines between nodes on map
- [ ] Implement waypoint creation and sharing
- [ ] Add distance/bearing calculations to other nodes
- [ ] **Background Service Implementation**:
  - [ ] Foreground service for BLE connection maintenance
  - [ ] Screen-off operation (maintain BLE comms when screen locked)
  - [ ] Message notifications (push notifications for incoming messages)
  - [ ] Battery optimization handling (Doze mode exemption)
  - [ ] Wake locks for critical operations
- [ ] Begin offline map tile caching

---

## Core Features

### Must-Have Features (Version 1)
- [x] Offline map storage and display *(Using osmdroid with OpenStreetMap)*
  - [ ] User-selectable map providers (like ATAK)
  - [ ] User-selectable map styles (satellite, terrain, topo, street)
  - [ ] Download selected areas offline
- [x] Real-time position sharing (public & private channels) *(Telemetry system operational)*
  - [x] **Location privacy controls**: *(Telemetry configurable per channel)*
    - [x] Optional location sharing on public channel (user choice)
    - [x] Required location sharing on private channels
    - [x] Per-channel location sharing settings *(Telemetry channel selection in settings)*
  - [ ] **Compass heading** on user's own marker
  - [ ] **Distance rings** (configurable, default 500m increments)
- [x] Text messaging (public & private channels) *(Full messaging system complete)*
- [ ] **Background operation & notifications**:
  - [ ] Foreground service to maintain BLE connection when screen is off
  - [ ] Push notifications for incoming messages
  - [x] Background location polling for position sharing *(WorkManager telemetry)*
  - [ ] Battery optimization handling (prevent Doze from killing service)
  - [ ] Persistent notification showing connection status
- [x] **Message delivery tracking with color-coded markers**: *(Map connectivity status operational)*
  - [x] 🟢 Green = Direct contact (heard directly, no hops) *(ConnectivityStatus.DIRECT)*
  - [x] 🟡 Yellow = Via mesh (received through hops) *(ConnectivityStatus.RELAYED 1-3 hops)*
  - [x] 🟠 Orange = Distant (4+ hops) *(ConnectivityStatus.DISTANT)*
  - [x] 🔴 Red = Stale (not heard in X seconds) *(ConnectivityStatus.OFFLINE)*
  - [x] Show message as "Sent" when transmitted *(Delivery status in message bubbles)*
  - [x] Show "Delivered" with count when ACKs received *(ACK tracking system)*
  - [x] Display which devices/users heard the message *(ACK database records)*
  - [x] Track direct contacts (who heard you directly) *(isDirect flag, hop count tracking)*
  - [x] Real-time range/distance management *(Last seen tracking, hop count from telemetry)*
- [x] BLE connection to companion radio *(Full BLE stack operational)*
- [x] **Channel management**: *(Complete channel system)*
  - [x] Public channel (default, open to all)
  - [x] Private channels (shared key, invitation-based) *(PSK generation, QR/link sharing)*
  - [x] Per-channel settings (location sharing, notifications) *(Telemetry channel selection)*

### Version 2 Features (Post-MVP)
- [ ] Waypoint creation and sharing
- [ ] Property boundary overlays (if not too complex)

### Nice-to-Have Features
- [ ] Distance/bearing to other users
- [x] Battery level indicators for mesh nodes *(Stored in NodeEntity from telemetry)*
- [x] ~~Message delivery confirmations~~ ✅ Implemented with ACK tracking
- [ ] **Advanced delivery tracking**:
  - [ ] Message propagation visualization on map
  - [ ] Show mesh path/hops for each delivery
  - [ ] Signal quality heatmap based on RTT
  - [ ] Historical range data per contact
- [ ] Group/contact management
- [ ] Quick status updates ("At Stand #3", "Tracking", etc.)
- [ ] Game markers/sighting locations

### Maybe Later (Complexity vs Value TBD)
- [ ] Time-based location history (see where someone was 30 min ago)
- [ ] Track recording/breadcrumbs
- [ ] Property boundary overlays (KML/shapefile import)
- [ ] Photo sharing (compressed/thumbnail)

---

## Architecture Overview

### Three-Layer Architecture

#### 1. Hardware Communication Layer
- **BLE Connection** to MeshCore companion radio
- **Protocol Implementation** for packet send/receive
- **Connection Management** (reconnection, health monitoring)

#### 2. MeshCore Protocol Layer
- **Packet Encoding/Decoding**
  - Header construction (route type, payload type, version)
  - Path management
  - Payload serialization
- **Encryption/Decryption**
  - Ed25519 signatures
  - AES encryption for messages
- **Identity Management**
  - Public/private keypair generation and storage
  - Node hash calculation
- **Message Types Handled**
  - `PAYLOAD_TYPE_ADVERT` - location broadcasts
  - `PAYLOAD_TYPE_GRP_TXT` - group messages
  - `PAYLOAD_TYPE_TXT_MSG` - private messages
  - `PAYLOAD_TYPE_GRP_DATA` - waypoint data (custom)

#### 3. Application Layer
- **Map Display & Interaction**
- **Message UI**
- **Waypoint Management**
- **Settings & Configuration**

---

## Technical Stack

### Core Technologies
- **Language**: Kotlin
- **Min SDK**: 26 (Android 8.0)
- **Architecture**: MVVM + Clean Architecture
- **Build System**: Gradle (Kotlin DSL)

### Key Libraries

#### Communication & Crypto
- [ ] Android BLE API (built-in)
- [ ] BouncyCastle or Tink (Ed25519 + AES)
- [ ] Kotlin Coroutines (async operations)

#### Maps & Location
- [ ] **Map Provider** (choose one):
  - osmdroid (OpenStreetMap)
  - Mapbox SDK
  - Google Maps SDK
- [ ] Android Location Services
- [ ] Fused Location Provider

#### Data & Storage
- [ ] Room Database (messages, waypoints, nodes)
- [ ] DataStore (preferences)
- [ ] Storage Access Framework (map tiles)

#### UI
- [ ] **UI Framework** (choose one):
  - Jetpack Compose (modern)
  - Traditional XML layouts
- [ ] Material Design 3 components
- [ ] Navigation Component

---

## Data Models

### Node
```kotlin
data class MeshNode(
    val publicKey: ByteArray,        // 32 bytes
    val hash: Byte,                  // First byte of public key
    val name: String?,               // Optional node name
    val latitude: Double?,           // Location if available
    val longitude: Double?,
    val lastSeen: Long,              // Unix timestamp
    val batteryMilliVolts: Int?,     // Battery level
    val isRepeater: Boolean,
    val isRoomServer: Boolean
)
```

### Message
```kotlin
data class Message(
    val id: String,                  // UUID
    val senderId: ByteArray,         // Sender's public key
    val senderName: String?,         // Cached sender name
    val channelHash: Byte,           // Channel identifier
    val content: String,             // Message text
    val timestamp: Long,             // Unix timestamp
    val isPrivate: Boolean,          // Private vs group message
    val ackChecksum: ByteArray?,     // 4-byte ACK checksum for tracking
    val deliveryStatus: DeliveryStatus, // Sending, Sent, Delivered
    val heardByCount: Int,           // How many devices ACKed
    val heardByDevices: List<ByteArray>, // Public keys of devices that ACKed
    val directContacts: List<ByteArray>, // Devices that heard directly (for range)
    val roundTripTimes: Map<ByteArray, Long>, // RTT per device in ms
    val attempt: Int                 // Retry count (0-3)
)

enum class DeliveryStatus {
    SENDING,     // Message queued/transmitting
    SENT,        // Transmitted successfully (RESP_CODE_SENT)
    DELIVERED    // At least one ACK received (PUSH_CODE_SEND_CONFIRMED)
}
```

### Waypoint
```kotlin
data class Waypoint(
    val id: String,                  // UUID
    val name: String,                // User-defined name
    val latitude: Double,
    val longitude: Double,
    val createdBy: ByteArray,        // Creator's public key
    val createdByName: String?,      // Cached creator name
    val timestamp: Long,             // Creation time
    val icon: String,                // Icon identifier
    val channelHash: Byte            // Channel it was shared on
)
```

### Channel
```kotlin
data class Channel(
    val hash: Byte,                  // SHA256(sharedKey)[0]
    val name: String,                // User-defined name
    val sharedKey: ByteArray,        // 16-byte AES key
    val isPublic: Boolean,           // Public vs private
    val shareLocation: Boolean,      // Whether to share location on this channel
    val createdAt: Long              // Creation timestamp
)

// Location sharing logic:
// - Public channel: shareLocation = user preference (default false)
// - Private channel: shareLocation = always true (required, enforced in code)
```

---

## BLE Communication Protocol

### BLE Service & Characteristics
The companion radio uses Nordic UART Service (NUS) compatible UUIDs:

```
Service UUID:          6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX Characteristic:     6E400002-B5A3-F393-E0A9-E50E24DCCA9E (WRITE)
TX Characteristic:     6E400003-B5A3-F393-E0A9-E50E24DCCA9E (READ + NOTIFY)
```

**Source**: `src/helpers/esp32/SerialBLEInterface.cpp`

### BLE Security
- **Authentication**: ESP_LE_AUTH_REQ_SC_MITM_BOND (Secure Connections + MITM protection + Bonding)
- **Static PIN**: 6-digit PIN code (configurable, stored in `NodePrefs.ble_pin`)
- **Permissions**: 
  - RX: `ESP_GATT_PERM_WRITE_ENC_MITM` (encrypted write)
  - TX: `ESP_GATT_PERM_READ_ENC_MITM` (encrypted read)

### Frame Protocol
- **Max Frame Size**: 172 bytes (`MAX_FRAME_SIZE`)
- **Frame Format**: Length-prefixed binary frames
- **Queuing**: Both TX and RX use 4-frame queues for buffering
- **Write Interval**: Minimum 60ms between writes to avoid overwhelming BLE stack

### Command/Response Structure

All commands sent to the device start with a command byte, followed by parameters:

#### Key Commands (from `MyMesh.cpp`)
```
CMD_APP_START                 = 1   // Initialize connection
CMD_SEND_TXT_MSG              = 2   // Send private message
CMD_SEND_CHANNEL_TXT_MSG      = 3   // Send group/channel message  
CMD_GET_CONTACTS              = 4   // Sync contact list
CMD_GET_DEVICE_TIME           = 5   // Get device timestamp
CMD_SET_DEVICE_TIME           = 6   // Set device timestamp
CMD_SEND_SELF_ADVERT          = 7   // Broadcast node advertisement
CMD_SET_ADVERT_NAME           = 8   // Set node name
CMD_SET_ADVERT_LATLON         = 14  // Set broadcast location
CMD_SYNC_NEXT_MESSAGE         = 10  // Poll for new messages
CMD_GET_CHANNEL               = 31  // Get channel info
CMD_SET_CHANNEL               = 32  // Add/update channel
```

#### Response Codes
```
RESP_CODE_OK                  = 0   // Command succeeded
RESP_CODE_ERR                 = 1   // Command failed
RESP_CODE_SELF_INFO           = 5   // Device info (reply to APP_START)
RESP_CODE_SENT                = 6   // Message sent confirmation
RESP_CODE_CONTACT_MSG_RECV_V3 = 16  // Private message received
RESP_CODE_CHANNEL_MSG_RECV_V3 = 17  // Channel message received
RESP_CODE_NO_MORE_MESSAGES    = 10  // No messages to sync
```

#### Push Notifications (Async)
These are sent from device to app at any time:
```
PUSH_CODE_ADVERT              = 0x80  // Node advertisement received
PUSH_CODE_NEW_ADVERT          = 0x8A  // New node detected
PUSH_CODE_MSG_WAITING         = 0x83  // Message ready to sync
PUSH_CODE_SEND_CONFIRMED      = 0x82  // Message delivery confirmed (ACK received)
PUSH_CODE_PATH_UPDATED        = 0x81  // Route to node updated
```

**PUSH_CODE_SEND_CONFIRMED Format** (9 bytes):
```
Byte 0:     0x82 (push code)
Bytes 1-4:  ACK checksum (4 bytes)
Bytes 5-8:  Round-trip time in milliseconds (4 bytes)
```

**Important**: The same message can trigger multiple `PUSH_CODE_SEND_CONFIRMED` notifications as different nodes receive and ACK the message. The app should:
- Track each unique ACK to count how many devices heard the message
- Display the list of devices that acknowledged
- Show devices with direct path (path_len = 0xFF) separately for range management

### Connection Flow

1. **Scan** for BLE devices advertising service UUID `6E400001-...`
2. **Connect** to device
3. **Pair** using PIN code (default or user-configured)
4. **Subscribe** to TX characteristic notifications
5. **Send** `CMD_APP_START` to initialize
6. **Receive** `RESP_CODE_SELF_INFO` with device details
7. **Poll** with `CMD_SYNC_NEXT_MESSAGE` for incoming messages
8. **Listen** for async `PUSH_CODE_*` notifications

### Supported Boards for Development

Based on `examples/companion_radio/main.cpp`, BLE is supported on:

- **ESP32** (DEVELOPMENT HARDWARE) ✅
  - Full BLE support via ESP32 BLE stack
  - Implementation: `src/helpers/esp32/SerialBLEInterface.{h,cpp}`
  - **Development Board**: LilyGO T-Beam (in stock for testing)
  - Other options: Heltec V3, TTGO boards
  
- **nRF52840** (TARGET HARDWARE) 🎯
  - Full BLE support via Nordic SoftDevice
  - Implementation: `src/helpers/nrf52/SerialBLEInterface.{h,cpp}`
  - **Target Board**: Heltec T114 V2 (power-efficient for field use)
  - Other options: RAK4631, Adafruit nRF52840

- **RP2040** - BLE support commented out (not ready)
- **STM32** - No BLE support

**Development Strategy**: 
1. **Phase 1-5**: Develop and test with LilyGO T-Beam (ESP32)
2. **Phase 6**: Test compatibility with Heltec T114 V2 (nRF52840)
3. Both use same BLE protocol, so transition should be seamless

---

## MeshCore Protocol Details

### Packet Structure
```
| Field           | Size (bytes)  | Description                          |
|-----------------|---------------|--------------------------------------|
| header          | 1             | Route type, payload type, version    |
| transport_codes | 4 (optional)  | 2x 16-bit codes (if ROUTE_TRANSPORT) |
| path_len        | 1             | Path field length                    |
| path            | 0-64          | Routing path                         |
| payload         | 0-184         | Actual data                          |
```

### Header Byte
```
Bits 0-1: Route Type (FLOOD=0x01, DIRECT=0x02, etc.)
Bits 2-5: Payload Type (REQ=0x00, TXT_MSG=0x02, ADVERT=0x04, etc.)
Bits 6-7: Payload Version (V1=0x00)
```

### Location in Advertisements
```
latitude_int = latitude * 1,000,000
longitude_int = longitude * 1,000,000
(stored as 4-byte little-endian signed integers)
```

### Encryption
- **Ed25519**: Node identity and signatures
- **AES-128**: Message encryption (CTR or similar mode)
- **Cipher MAC**: 2-byte authentication code

---

## Implementation Phases

### Phase 1: Foundation (2-3 weeks)
**Goal**: Basic app structure and BLE connection

- [ ] Project setup (Gradle, dependencies)
- [ ] **BLE Implementation**:
  - [ ] Scan for devices with service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - [ ] Connect and pair with PIN authentication
  - [ ] Subscribe to TX characteristic notifications
  - [ ] Implement frame-based communication (172 byte max)
  - [ ] Handle 60ms minimum write intervals
  - [ ] Implement send/receive queues
- [ ] **Command Protocol**:
  - [ ] Implement `CMD_APP_START` 
  - [ ] Parse `RESP_CODE_SELF_INFO`
  - [ ] Handle `RESP_CODE_OK` / `RESP_CODE_ERR`
- [ ] Identity storage (device provides public key)
- [ ] Simple UI skeleton (MainActivity)
- [ ] Connection status indicator

**Deliverable**: App connects to companion radio via BLE and initializes session

**Key Files to Reference**: 
- `src/helpers/esp32/SerialBLEInterface.cpp` (BLE implementation)
- `examples/companion_radio/MyMesh.cpp` (command handling)

---

### Phase 2: Messaging (2-3 weeks)
**Goal**: Send and receive text messages

- [ ] **Channel management**:
  - [ ] Implement `CMD_GET_CHANNEL` 
  - [ ] Implement `CMD_SET_CHANNEL`
  - [ ] Create/join channels
  - [ ] Default public channel (PSK: `izOH6cXN6mrJ5e26oRXNcg==`)
  - [ ] **Per-channel location sharing settings**:
    - [ ] Public channel: Optional (default OFF for privacy)
    - [ ] Private channels: Required (always ON)
    - [ ] UI toggle for public channel location sharing
    - [ ] Store preference per channel
- [ ] **UI Improvements**:
  - [ ] Add persistent navigation bar for easy screen switching
  - [ ] Add sender name/tag to message bubbles (at top or bottom before timestamp)
- [ ] **Message sending**:
  - [ ] Implement `CMD_SEND_TXT_MSG` (private)
  - [ ] Implement `CMD_SEND_CHANNEL_TXT_MSG` (group)
  - [ ] Handle `RESP_CODE_SENT` confirmation
  - [ ] Listen for `PUSH_CODE_SEND_CONFIRMED`
- [ ] **Message receiving**:
  - [ ] Implement `CMD_SYNC_NEXT_MESSAGE` polling
  - [ ] Parse `RESP_CODE_CONTACT_MSG_RECV_V3` (private)
  - [ ] Parse `RESP_CODE_CHANNEL_MSG_RECV_V3` (group)
  - [ ] Handle `PUSH_CODE_MSG_WAITING` notification
  - [ ] Handle `RESP_CODE_NO_MORE_MESSAGES`
- [ ] **Message delivery tracking**:
  - [ ] Listen for `PUSH_CODE_SEND_CONFIRMED` notifications
  - [ ] Parse ACK checksum and round-trip time
  - [ ] Track multiple ACKs for same message (can receive multiple)
  - [ ] Update message delivery status in database
  - [ ] Count unique devices that heard message
  - [ ] Identify direct contacts (path_len = 0xFF in received messages)
- [ ] Message encryption/decryption (handled by device)
- [ ] Message database schema (with delivery tracking fields)
- [ ] Message list UI with delivery indicators
- [ ] Send message UI
- [ ] Message notifications
- [ ] **Direct contact display**:
  - [ ] Show which users heard your last message
  - [ ] Display signal strength/RTT for range estimation
  - [ ] Update in real-time as ACKs arrive

**Deliverable**: Can send/receive messages on public and private channels with full delivery tracking

**Key Commands**: 
- `CMD_SEND_TXT_MSG = 2`
- `CMD_SEND_CHANNEL_TXT_MSG = 3`
- `CMD_SYNC_NEXT_MESSAGE = 10`
- `CMD_GET_CHANNEL = 31`
- `CMD_SET_CHANNEL = 32`

**ACK Tracking Implementation Notes**:
- Device maintains an ACK table with up to 8 expected ACKs (`EXPECTED_ACK_TABLE_SIZE`)
- Each sent message has a 4-byte checksum calculated from timestamp, text, and sender pubkey
- When recipient receives message, they send back an ACK packet with this checksum
- **Multiple ACKs**: Same message can be ACKed by multiple nodes as it propagates through mesh
- App should track: checksum → list of (device_pubkey, round_trip_time, path_info)
- Use `path_len = 0xFF` in received messages to identify direct (single-hop) contacts

---

### Phase 3: Mapping (2-3 weeks)
**Goal**: Display offline maps with user location

- [ ] **Map provider setup**:
  - [ ] osmdroid library integration
  - [ ] Multiple tile source configuration (OSM, USGS, satellite, topo)
  - [ ] User-selectable map style in settings
- [ ] **Current location display**:
  - [ ] GPS position tracking
  - [ ] User marker with compass heading indicator
  - [ ] Orientation sensor integration
- [ ] **Distance rings**:
  - [ ] Configurable distance rings around user (default 500m increments)
  - [ ] Toggle on/off in settings
  - [ ] Customizable distance intervals
- [ ] **Offline tile management**:
  - [ ] Map tile download mechanism
  - [ ] Area selection for download
  - [ ] Cache management
  - [ ] Storage usage display
- [ ] Basic map controls (zoom, pan, rotation)

**Deliverable**: Can view offline maps with compass-enabled user marker and distance rings

---

### Phase 4: Position Sharing (1-2 weeks)
**Goal**: Share and display positions of other users

- [ ] **Location Broadcasting**:
  - [ ] Get device GPS location (if available)
  - [ ] Implement `CMD_SET_ADVERT_LATLON` to set position
  - [ ] Implement `CMD_SEND_SELF_ADVERT` to broadcast
  - [ ] **Privacy-aware broadcasting**:
    - [ ] Check channel location sharing settings before broadcast
    - [ ] If on public channel with location OFF: don't broadcast position
    - [ ] If on private channel: always broadcast (required)
    - [ ] Visual indicator in UI showing location sharing status
  - [ ] Set location update frequency (60-120 seconds recommended)
- [ ] **Location Receiving**:
  - [ ] Listen for `PUSH_CODE_ADVERT` (known contact)
  - [ ] Listen for `PUSH_CODE_NEW_ADVERT` (new node)
  - [ ] Parse advertisement data (lat/lon in advertisements)
  - [ ] Extract location from advertisements (lat/lon * 1,000,000)
- [ ] **Contact Management**:
  - [ ] Implement `CMD_GET_CONTACTS` to sync contacts
  - [ ] Parse `RESP_CODE_CONTACT` (multiple)
  - [ ] Parse `RESP_CODE_END_OF_CONTACTS`
  - [ ] Store contacts with last known positions
- [ ] **Display other users on map with color-coded markers**:
  - [ ] 🟢 Green marker = Direct contact (path_len = 0xFF, within radio range)
  - [ ] 🟡 Yellow marker = Via mesh (received through hops)
  - [ ] 🔴 Red marker = Stale (not heard in X seconds, configurable)
  - [ ] Show user names/labels
  - [ ] Tap marker for details (last seen, RTT, hop count)
- [ ] Update positions in real-time
- [ ] User list with last seen times and color-coded status
- [ ] Filter by channel
- [ ] Configurable "stale" timeout (default 5 minutes)

**Deliverable**: Can see locations of other users on map with visual indication of connection quality

**Key Commands**: 
- `CMD_SEND_SELF_ADVERT = 7`
- `CMD_SET_ADVERT_LATLON = 14`
- `CMD_GET_CONTACTS = 4`

**Push Codes**:
- `PUSH_CODE_ADVERT = 0x80`
- `PUSH_CODE_NEW_ADVERT = 0x8A`

---

### Phase 5: Waypoints (1-2 weeks) - VERSION 2
**Goal**: Create, share, and display waypoints
**Note**: Not required for Version 1 MVP

- [ ] Waypoint creation UI (long-press map)
- [ ] Waypoint data structure
- [ ] **Custom waypoint protocol**:
  - [ ] Use `CMD_SEND_CHANNEL_TXT_MSG` with structured data
  - [ ] Format: JSON or binary format with type marker
  - [ ] Include: name, lat, lon, timestamp, icon, creator
- [ ] Encode/decode waypoint messages
- [ ] Waypoint database
- [ ] Display waypoints on map
- [ ] Waypoint list view
- [ ] Edit/delete waypoints
- [ ] Share waypoints on selected channel

**Deliverable**: Can create and share waypoints with other users

**Implementation Note**: Waypoints will be sent as channel messages with a special format that the app recognizes. The companion radio treats them as regular group messages.

---

### Phase 6: Polish & Testing (2-3 weeks)
**Goal**: Production-ready app

- [ ] Battery optimization
- [ ] Background service for BLE
- [ ] Foreground notification
- [ ] Error handling and recovery
- [ ] Offline mode handling
- [ ] Settings screen
- [ ] App permissions management
- [ ] User onboarding/tutorial
- [ ] Testing on multiple devices
- [ ] Performance optimization
- [ ] Documentation

**Deliverable**: Release candidate ready for field testing

---

## Key Challenges & Solutions

### Challenge: BLE Protocol Documentation
**Status**: � Documented
- **Issue**: ✅ SOLVED - Full BLE protocol available in source code
- **Solution**: Use `src/helpers/esp32/SerialBLEInterface.cpp` as reference
- **Reference**: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- **Action Items**:
  - [x] Find source code - Available in `src/helpers/`
  - [x] Document BLE service UUIDs - See BLE Protocol Details section below
  - [x] Document characteristic UUIDs and formats - See BLE Protocol Details section below

### Challenge: Cryptography Implementation
**Status**: 🔴 Not Started
- **Issue**: Must match MeshCore's exact Ed25519 and AES implementation
- **Solution**: Use BouncyCastle library, test against known vectors
- **Action Items**:
  - [ ] Implement Ed25519 key generation
  - [ ] Implement signing/verification
  - [ ] Implement AES encryption/decryption
  - [ ] Create unit tests with test vectors from firmware

### Challenge: Battery Life
**Status**: 🔴 Not Started
- **Issue**: BLE + GPS + screen = high power consumption
- **Solution**: Optimize background operations, adjustable update intervals
- **Action Items**:
  - [ ] Implement configurable location update frequency
  - [ ] Use WorkManager for background tasks
  - [ ] Implement doze mode handling
  - [ ] Test battery drain in field conditions

### Challenge: Map Storage
**Status**: 🔴 Not Started
- **Issue**: Offline maps can be very large (GB+)
- **Solution**: Selective area downloads, cache management, compression
- **Action Items**:
  - [ ] Implement zoom-level based tile downloading
  - [ ] Set maximum cache size limits
  - [ ] Implement LRU cache eviction
  - [ ] Show storage usage in settings

### Challenge: Mesh Network Education
**Status**: 🔴 Not Started
- **Issue**: Users may not understand mesh limitations (range, hops, latency)
- **Solution**: Clear UI indicators, onboarding tutorial, help section
- **Action Items**:
  - [ ] Create "How It Works" tutorial
  - [ ] Show signal strength indicators
  - [ ] Display hop count to users
  - [ ] Add tips/warnings for best practices

---

## Resources & References

### Resources & References

### MeshCore Documentation
- [Packet Structure](docs/packet_structure.md)
- [Payload Formats](docs/payloads.md)
- [FAQ](docs/faq.md)

### MeshCore Source Code (Critical References)
- **BLE Protocol**: 
  - `src/helpers/esp32/SerialBLEInterface.{h,cpp}` - ESP32 BLE implementation
  - `src/helpers/nrf52/SerialBLEInterface.{h,cpp}` - nRF52 BLE implementation
  - `src/helpers/BaseSerialInterface.h` - Base interface definition
- **Companion Radio**:
  - `examples/companion_radio/main.cpp` - Main application entry point
  - `examples/companion_radio/MyMesh.{h,cpp}` - Command/response handling (1868 lines!)
  - `examples/companion_radio/NodePrefs.h` - Configuration structure
  - `examples/companion_radio/DataStore.{h,cpp}` - Persistent storage

### Existing Clients
- **Android**: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- **Web**: https://app.meshcore.nz
- **iOS**: https://apps.apple.com/us/app/meshcore/id6742354151
- **NodeJS**: https://github.com/liamcottle/meshcore.js
- **Python**: https://github.com/fdlamotte/meshcore-cli

### Development Tools
- [ ] Android Studio (latest stable)
- [ ] MeshCore companion radio device
- [ ] BLE debugging tools (nRF Connect)
- [ ] Wireshark (for packet analysis if needed)

---

## Development Environment Setup

### Prerequisites
- [ ] Android Studio installed
- [ ] Physical Android device (API 26+)
- [ ] **MeshCore Hardware**:
  - [ ] LilyGO T-Beam (ESP32) - Development/testing (in stock)
  - [ ] Heltec T114 V2 (nRF52840) - Field testing/production (order later)
  - [ ] Companion firmware flashed on device
  - [ ] BLE configured with PIN authentication
- [ ] Git for version control

### Initial Setup Steps
1. [ ] Create new Android project (Kotlin, MVVM)
2. [ ] Set up version control (Git repository)
3. [ ] Configure Gradle dependencies
4. [ ] Request necessary permissions in manifest
5. [ ] Create base package structure

### Version 1 MVP (Maps + Position + Messaging)
**Total Time**: 10-12 weeks (2.5-3 months part-time)

| Phase | Duration | Target Completion |
|-------|----------|-------------------|
| Phase 1: Foundation | 2-3 weeks | TBD |
| Phase 2: Messaging | 2-3 weeks | TBD |
| Phase 3: Mapping | 2-3 weeks | TBD |
| Phase 4: Position Sharing | 1-2 weeks | TBD |
| Phase 6: Polish & Field Testing | 2-3 weeks | TBD |

### Version 2 (Add Waypoints)
**Additional Time**: 2-3 weeks

| Phase | Duration | Target Completion |
|-------|----------|-------------------|
| Phase 5: Waypoints | 1-2 weeks | TBD |
| Phase 6b: Polish & Testing | 1 weekMapbox, Thunderforest
6. **Frequency**: How often to broadcast location updates?
   - Device broadcasts advertisements periodically
   - App can trigger with CMD_SEND_SELF_ADVERT
   - Suggested: Every 60-120 seconds to balance battery and freshness
- [ ] Cryptography functions
- [ ] Data model validation
- [ ] Utility functions

### Integration Tests
- [ ] BLE communication
- [ ] Database operations
- [ ] Channel management
- [ ] Message flow

### Field Tests
- [ ] Real-world range testing
- [ ] Multi-hop message delivery
- [ ] Battery life under typical use
- [ ] Map performance with large areas
- [ ] Multiple devices simultaneously

---

## Timeline Estimate

**Total Time**: 12-16 weeks (3-4 months part-time)

| Phase | Duration | Target Completion |
|-------|----------|-------------------|
| Phase 1: Foundation | 2-3 weeks | TBD |
| Phase 2: Messaging | 2-3 weeks | TBD |
| Phase 3: Mapping | 2-3 weeks | TBD |
| Phase 4: Position Sharing | 1-2 weeks | TBD |
| Phase 5: Waypoints | 1-2 weeks | TBD |
| Phase 6: Polish | 2-3 weeks | TBD |

---

## Decision Log

### Map Provider Decision
**Status**: � Decided
**Decision**: **Multi-provider support (user-selectable like ATAK)**

**Implementation Strategy**:
1. **Primary/Default**: osmdroid (OpenStreetMap)
   - Free, open source, excellent offline support
   - Multiple tile source options (OpenStreetMap, USGS Topo, satellite, etc.)
   - User can add custom tile sources
   
2. **Optional**: Mapbox SDK integration
   - For users who want premium satellite imagery
   - Pay-per-use, but high quality
   - Can be added as plugin/upgrade

3. **Map Style Options** (within osmdroid):
   - Street map (default OpenStreetMap)
   - Satellite imagery (USGS, Bing, etc.)
   - Topographic (USGS Topo, OpenTopoMap)
   - Terrain

**Why This Approach**:
- Flexibility like ATAK (user picks what they need)
- Start free (osmdroid), add premium options later
- No vendor lock-in
- Best offline capabilities

---

### UI Framework Decision
**Status**: � Decided
**Decision**: **Jetpack Compose** (modern standard)

**Rationale**:
- Industry standard for new Android apps (since 2021)
- Less boilerplate code than XML
- Better integration with modern Android architecture (MVVM, StateFlow)
- Excellent with osmdroid (osmdroid-compose library available)
- Easier state management for real-time updates (messages, positions)
- Future-proof (Google's focus going forward)

**Learning Resources**:
- Official Android Compose tutorials
- Compose is actually easier for beginners (more intuitive than XML)
- Clear separation of UI and logic

**Map Integration**:
- Use `AndroidView` to embed osmdroid MapView in Compose
- Or use compose-osmdroid wrapper library

---

## Notes & Ideas

### Future Enhancements (Post-MVP)
- Track recording/breadcrumbs for routes taken
- Emergency SOS broadcast
- Pre-defined message templates
- Compass/bearing to selected user or waypoint
- Weather overlay if available
- Topographic map support
- Night mode for map
- Voice message recording (sent as data)
- Photo sharing (compressed/thumbnail)
- Mesh network health visualization
- **Advanced ACK Analytics**:
  - Message propagation timeline (which nodes received when)
  - Network coverage heat map based on delivery success rate
  - Per-contact reliability metrics
  - Optimal transmit power recommendations based on ACK patterns
  - "Who can hear who" mesh topology visualization

### Security Considerations
- [ ] Secure key storage (Android Keystore)
- [ ] Channel key sharing mechanism (QR code?)
- [ ] Private key backup/recovery
- [ ] **Location privacy**:
  - [ ] Public channel: Location sharing OFF by default
  - [ ] Private channels: Location sharing required (always ON)
  - [ ] Clear UI indicators showing when location is being shared
  - [ ] Warning when switching to public channel with location ON
- [ ] Screen lock requirement?

---

## Open Questions

1. **BLE Protocol**: What are the exact BLE service/characteristic UUIDs?
2. **Waypoint Format**: Should we use PAYLOAD_TYPE_GRP_DATA or create custom type?
3. **Location Privacy**: Option to disable location sharing on certain channels?
4. **Channel Discovery**: How do users discover/join private channels?
5. **Map Source**: Which OpenStreetMap tile server to use?
6. **Frequency**: How often to broadcast location updates? (every 30s, 60s, 5min?)

---

## Status Legend
- 🔴 Not Started
- 🟡 In Progress
- 🟢 Completed
- ⚠️ Blocked
