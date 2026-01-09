# TEAM Architecture

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Android Device                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              TEAM Android App                         │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │          Presentation Layer                     │  │  │
│  │  │  • Map UI (osmdroid/Mapbox)                     │  │  │
│  │  │  • Message List UI                              │  │  │
│  │  │  • Waypoint UI                                  │  │  │
│  │  │  • Contact/Delivery Tracking UI                 │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │          Application Layer                      │  │  │
│  │  │  • Message Manager                              │  │  │
│  │  │  • Position Manager                             │  │  │
│  │  │  • Waypoint Manager                             │  │  │
│  │  │  • Channel Manager                              │  │  │
│  │  │  • Delivery Tracker (ACK handling)              │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │          Data Layer                             │  │  │
│  │  │  • Room Database (messages, waypoints, nodes)   │  │  │
│  │  │  • DataStore (preferences)                      │  │  │
│  │  │  • Map Tile Cache                               │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │     BLE Communication Layer                     │  │  │
│  │  │  • BLE Scanner & Connector                      │  │  │
│  │  │  • Command/Response Handler                     │  │  │
│  │  │  • Frame Queue Manager                          │  │  │
│  │  │  • Push Notification Handler                    │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
│                           ↕ BLE                             │
└─────────────────────────────────────────────────────────────┘
                            ↕
┌─────────────────────────────────────────────────────────────┐
│              MeshCore Companion Radio                       │
│              (T-Beam / T114 V2)                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  • BLE Server (Nordic UART Service)                  │  │
│  │  • Command Processing                                │  │
│  │  • Mesh Protocol (routing, encryption)               │  │
│  │  • LoRa Radio Interface                              │  │
│  └───────────────────────────────────────────────────────┘  │
│                        ↕ LoRa                               │
└─────────────────────────────────────────────────────────────┘
                         ↕
              ┌──────────────────────┐
              │   Mesh Network       │
              │  (Other devices)     │
              └──────────────────────┘
```

## Component Details

### BLE Communication Layer

**Responsibilities:**
- Scan for MeshCore devices
- Connect and authenticate via PIN
- Send command frames (max 172 bytes)
- Receive response frames
- Handle async push notifications
- Maintain connection health

**Key Classes:**
- `BleConnectionManager`: Manages BLE lifecycle
- `CommandSerializer`: Encodes commands to binary frames
- `ResponseParser`: Decodes binary responses
- `PushNotificationHandler`: Processes async notifications

### Application Layer

**Message Manager:**
- Send text messages (private/channel)
- Poll for incoming messages
- Track message delivery status
- Process ACK notifications
- Maintain delivery statistics

**Position Manager:**
- Get device GPS location
- Broadcast location to mesh
- Parse incoming location advertisements
- Update node positions in real-time

**Waypoint Manager:**
- Create/edit/delete waypoints
- Encode waypoints for transmission
- Parse incoming waypoint messages
- Display on map

**Channel Manager:**
- Create/join channels
- Manage channel keys
- Default public channel handling

**Delivery Tracker:**
- Track expected ACKs
- Process `PUSH_CODE_SEND_CONFIRMED` notifications
- Count unique receivers
- Identify direct contacts
- Calculate statistics

### Data Layer

**Room Database Entities:**
```kotlin
@Entity(tableName = "messages")
data class MessageEntity(
    @PrimaryKey val id: String,
    val senderId: ByteArray,
    val channelHash: Byte,
    val content: String,
    val timestamp: Long,
    val ackChecksum: ByteArray?,
    val deliveryStatus: String,
    // ... delivery tracking fields
)

@Entity(tableName = "nodes")
data class NodeEntity(
    @PrimaryKey val publicKey: ByteArray,
    val name: String?,
    val latitude: Double?,
    val longitude: Double?,
    val lastSeen: Long,
    // ...
)

@Entity(tableName = "waypoints")
data class WaypointEntity(
    @PrimaryKey val id: String,
    val name: String,
    val latitude: Double,
    val longitude: Double,
    // ...
)

@Entity(tableName = "ack_records")
data class AckRecordEntity(
    @PrimaryKey(autoGenerate = true) val id: Long,
    val messageId: String,
    val ackChecksum: ByteArray,
    val devicePublicKey: ByteArray,
    val roundTripTime: Long,
    val isDirect: Boolean,
    val receivedAt: Long
)
```

## Data Flow Examples

### Sending a Message

```
User Input
    ↓
Message Manager
    ↓
Command: CMD_SEND_CHANNEL_TXT_MSG (3)
    ↓
CommandSerializer
    ↓
BLE Write (RX Characteristic)
    ↓
Companion Radio
    ↓
Response: RESP_CODE_SENT (6)
    ↓
Update message status to "SENT"
    ↓
[Async] PUSH_CODE_SEND_CONFIRMED (0x82)
    ↓
Delivery Tracker
    ↓
Update message status to "DELIVERED"
Add ACK record
    ↓
UI Update (show delivery count)
```

### Receiving a Position Update

```
Companion Radio receives ADVERT
    ↓
Push: PUSH_CODE_ADVERT (0x80)
    ↓
BLE Notification (TX Characteristic)
    ↓
PushNotificationHandler
    ↓
Parse public key, lat, lon, name
    ↓
Position Manager
    ↓
Update NodeEntity in database
    ↓
LiveData observer
    ↓
Update marker on map
```

## Thread Model

### Main Thread
- UI rendering
- User interactions
- LiveData observations

### IO Thread (Coroutines)
- Database operations
- File I/O (map tiles)
- Network operations

### BLE Thread
- BLE callbacks
- Frame queue processing
- Command/response handling

### Background Service
- Periodic location broadcasts
- Message polling
- Connection monitoring
- ACK timeout handling

## Security Considerations

### Key Storage
- Device public/private key stored in encrypted SharedPreferences or Android Keystore
- Channel keys stored in encrypted database
- BLE PIN stored securely

### Data Protection
- Messages encrypted by companion radio (Ed25519 + AES)
- App only handles ciphertext
- ACK checksums used for delivery tracking (not for authentication)

### Permissions
- `BLUETOOTH` - BLE communication
- `BLUETOOTH_ADMIN` - BLE scanning
- `BLUETOOTH_SCAN` (API 31+)
- `BLUETOOTH_CONNECT` (API 31+)
- `ACCESS_FINE_LOCATION` - GPS & BLE scanning
- `WRITE_EXTERNAL_STORAGE` - Map tiles (API < 29)
- `FOREGROUND_SERVICE` - Background operation

## State Management

### Connection States
```
DISCONNECTED
    ↓ scan & connect
CONNECTING
    ↓ pairing complete
AUTHENTICATING
    ↓ CMD_APP_START success
CONNECTED
    ↓ send commands
READY
```

### Message States
```
COMPOSING
    ↓ send
SENDING
    ↓ RESP_CODE_SENT
SENT
    ↓ PUSH_CODE_SEND_CONFIRMED
DELIVERED (with count)
```

## Error Handling

### BLE Errors
- Connection lost → Retry with exponential backoff
- GATT error 133 → Clear cache and reconnect
- MTU negotiation failure → Use minimum frame size

### Command Errors
- Timeout → Retry with backoff
- `RESP_CODE_ERR` → Parse error code and show user
- Queue full → Delay and retry

### Data Errors
- Database corruption → Backup and recreate
- Map tile missing → Download or show blank
- GPS unavailable → Disable location sharing

## Performance Optimizations

### BLE
- Batch commands when possible
- Respect 60ms write interval
- Use queues to avoid blocking

### Database
- Indexed queries on timestamp, channel
- Pagination for large message lists
- Background cleanup of old data

### Map
- Lazy loading of tiles
- LRU cache for markers
- Viewport culling for many nodes

### Battery
- Adjustable location broadcast interval
- Doze mode whitelist
- Efficient wake locks
