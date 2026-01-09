# BLE Protocol Reference

Quick reference for MeshCore BLE communication protocol.

## Service & Characteristics

```
Service UUID:     6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX Characteristic: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E (WRITE)
TX Characteristic: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E (READ + NOTIFY)
```

**Based on**: Nordic UART Service (NUS)

## Frame Format

- **Max Size**: 172 bytes
- **Min Write Interval**: 60ms
- **Format**: Binary frames with command/response codes

## Essential Commands

### Initialization
```
CMD_APP_START = 1
→ RESP_CODE_SELF_INFO = 5
  Returns: public key, node name, device info
```

### Messaging
```
CMD_SEND_TXT_MSG = 2
  Format: [cmd][32-byte pubkey][4-byte timestamp][text]
→ RESP_CODE_SENT = 6

CMD_SEND_CHANNEL_TXT_MSG = 3
  Format: [cmd][1-byte channel hash][4-byte timestamp][text]
→ RESP_CODE_SENT = 6

CMD_SYNC_NEXT_MESSAGE = 10
→ RESP_CODE_CONTACT_MSG_RECV_V3 = 16 (private message)
→ RESP_CODE_CHANNEL_MSG_RECV_V3 = 17 (channel message)
→ RESP_CODE_NO_MORE_MESSAGES = 10
```

### Position
```
CMD_SET_ADVERT_LATLON = 14
  Format: [cmd][4-byte lat * 1000000][4-byte lon * 1000000]
→ RESP_CODE_OK = 0

CMD_SEND_SELF_ADVERT = 7
→ RESP_CODE_OK = 0
```

### Contacts
```
CMD_GET_CONTACTS = 4
→ RESP_CODE_CONTACTS_START = 2
→ RESP_CODE_CONTACT = 3 (multiple)
→ RESP_CODE_END_OF_CONTACTS = 4
```

### Channels
```
CMD_GET_CHANNEL = 31
  Format: [cmd][1-byte channel hash]
→ RESP_CODE_CHANNEL_INFO = 18

CMD_SET_CHANNEL = 32
  Format: [cmd][1-byte hash][16-byte key][name string]
→ RESP_CODE_OK = 0
```

## Push Notifications (Async)

Sent from device at any time:

```
PUSH_CODE_ADVERT = 0x80
  Format: [0x80][32-byte pubkey][4-byte lat][4-byte lon][name]
  Received when known contact broadcasts position

PUSH_CODE_NEW_ADVERT = 0x8A
  Received when new node detected

PUSH_CODE_MSG_WAITING = 0x83
  Notification that message is ready to sync

PUSH_CODE_SEND_CONFIRMED = 0x82
  Format: [0x82][4-byte ACK checksum][4-byte RTT ms]
  Received when message is ACKed (can occur multiple times)

PUSH_CODE_PATH_UPDATED = 0x81
  Route to contact updated
```

## Message Structure Examples

### Send Channel Message
```kotlin
val frame = ByteArray(1 + 1 + 4 + text.length)
frame[0] = 3 // CMD_SEND_CHANNEL_TXT_MSG
frame[1] = channelHash
ByteBuffer.wrap(frame, 2, 4).order(ByteOrder.LITTLE_ENDIAN).putInt(timestamp)
text.toByteArray().copyInto(frame, 6)
```

### Set Location
```kotlin
val frame = ByteArray(1 + 4 + 4)
frame[0] = 14 // CMD_SET_ADVERT_LATLON
val latInt = (latitude * 1_000_000).toInt()
val lonInt = (longitude * 1_000_000).toInt()
ByteBuffer.wrap(frame, 1, 8).order(ByteOrder.LITTLE_ENDIAN)
    .putInt(latInt)
    .putInt(lonInt)
```

### Parse ACK Notification
```kotlin
// Push notification: PUSH_CODE_SEND_CONFIRMED
// Byte 0: 0x82
// Bytes 1-4: ACK checksum (4 bytes)
// Bytes 5-8: Round-trip time (4 bytes, little-endian)

val pushCode = frame[0] // 0x82
val ackChecksum = frame.copyOfRange(1, 5)
val rtt = ByteBuffer.wrap(frame, 5, 4)
    .order(ByteOrder.LITTLE_ENDIAN)
    .getInt()
```

## Response Codes

```kotlin
enum class ResponseCode(val value: Byte) {
    OK(0),
    ERR(1),
    CONTACTS_START(2),
    CONTACT(3),
    END_OF_CONTACTS(4),
    SELF_INFO(5),
    SENT(6),
    CONTACT_MSG_RECV(7),      // deprecated
    CHANNEL_MSG_RECV(8),      // deprecated
    CURR_TIME(9),
    NO_MORE_MESSAGES(10),
    EXPORT_CONTACT(11),
    BATT_AND_STORAGE(12),
    DEVICE_INFO(13),
    PRIVATE_KEY(14),
    DISABLED(15),
    CONTACT_MSG_RECV_V3(16),  // use this
    CHANNEL_MSG_RECV_V3(17),  // use this
    CHANNEL_INFO(18)
}

enum class PushCode(val value: Byte) {
    ADVERT(0x80.toByte()),
    PATH_UPDATED(0x81.toByte()),
    SEND_CONFIRMED(0x82.toByte()),
    MSG_WAITING(0x83.toByte()),
    RAW_DATA(0x84.toByte()),
    LOGIN_SUCCESS(0x85.toByte()),
    LOGIN_FAIL(0x86.toByte()),
    STATUS_RESPONSE(0x87.toByte()),
    LOG_RX_DATA(0x88.toByte()),
    TRACE_DATA(0x89.toByte()),
    NEW_ADVERT(0x8A.toByte()),
    TELEMETRY_RESPONSE(0x8B.toByte()),
    BINARY_RESPONSE(0x8C.toByte()),
    PATH_DISCOVERY_RESPONSE(0x8D.toByte()),
    CONTROL_DATA(0x8E.toByte())
}
```

## Error Codes

```kotlin
enum class ErrorCode(val value: Byte) {
    UNSUPPORTED_CMD(1),
    NOT_FOUND(2),
    TABLE_FULL(3),
    BAD_STATE(4),
    FILE_IO_ERROR(5),
    ILLEGAL_ARG(6)
}
```

## Connection Flow

1. Scan for devices with service UUID
2. Connect to device
3. Enable notifications on TX characteristic
4. Pair with PIN (6 digits)
5. Send `CMD_APP_START`
6. Wait for `RESP_CODE_SELF_INFO`
7. Parse device identity
8. Ready to send commands

## Best Practices

### Timing
- Wait 60ms between writes
- Use queues for multiple commands
- Handle async pushes immediately

### Error Handling
- Retry on timeout
- Parse error codes from `RESP_CODE_ERR`
- Reconnect on BLE disconnect

### Data Encoding
- Use little-endian for multi-byte integers
- UTF-8 for text (though device uses ASCII mostly)
- NULL-terminate strings where expected

### ACK Tracking
- Store ACK checksum when sending message
- Match against `PUSH_CODE_SEND_CONFIRMED` notifications
- Same message can have multiple ACKs
- Track per-device ACKs for delivery statistics

## Public Channel

The default public channel has a known PSK:
```kotlin
const val PUBLIC_CHANNEL_PSK = "izOH6cXN6mrJ5e26oRXNcg=="
// Hash: Calculate SHA256(base64decode(PSK))[0]
```

All devices can decode messages on this channel.
