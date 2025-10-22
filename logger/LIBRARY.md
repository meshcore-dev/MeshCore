# MeshCore Radio Client Library

A comprehensive TypeScript library for communicating with MeshCore companion radio firmware via the serial protocol. This library provides full support for sending commands and subscribing to incoming messages with full type safety.

## Overview

The library is organized into four main modules:

### 1. **Types** (`lib/types.ts`)
Defines all constants, enums, and TypeScript interfaces for the serial protocol:
- Command codes (`CommandCode`)
- Response codes (`ResponseCode`)
- Push codes (`PushCode`)
- Error codes (`ErrorCode`)
- Data structures (Contact, Message, DeviceInfo, etc.)
- Message types and handlers

### 2. **Frame Protocol** (`lib/frameProtocol.ts`)
Low-level frame encoding/decoding:
- `FrameParser` - State machine for parsing incoming frame bytes
- `encodeFrame()` - Encode payloads into complete frames
- Helper functions for creating commands with various argument types

### 3. **Message Decoder** (`lib/messageDecoder.ts`)
Decodes binary frames into typed TypeScript objects:
- `decodeFrame()` - Main decoder function
- Specialized decoders for each message type
- Helper functions for error messages and type guards

### 4. **Radio Client** (`lib/radioClient.ts`)
High-level client for the radio:
- `RadioClient` class with connection management
- Message subscription system
- All supported command methods
- Error handling

## Installation

The library uses the same dependencies as the existing logger project:

```bash
cd logger
npm install
# or
pnpm install
```

## Quick Start

### Basic Connection and Message Subscription

```typescript
import { RadioClient, ResponseMessage, isError } from './lib';

const radio = new RadioClient();

// Connect to radio (auto-detect or specify port)
await radio.connect('/dev/ttyUSB0');

// Subscribe to messages
const unsubscribe = radio.subscribe((message: ResponseMessage) => {
  if (isError(message)) {
    console.error(`Error: ${message.errorMessage}`);
  } else if (message.type === 'deviceInfo') {
    console.log(`Firmware: ${message.data.firmwareVersion}`);
  } else if (message.type === 'contactMessage') {
    console.log(`Message: ${message.data.text}`);
  }
});

// Send a command
radio.queryDeviceInfo();

// Later: unsubscribe
unsubscribe();

// Disconnect
radio.disconnect();
```

## API Reference

### RadioClient Class

#### Static Methods

```typescript
// Find available serial ports
const ports = await RadioClient.findAvailablePorts();
// Returns: Array<{ path: string; manufacturer?: string }>

// Auto-detect and connect
const radio = await RadioClient.autoDetectAndConnect();
```

#### Instance Methods

##### Connection
```typescript
connect(portPath: string, baudRate?: number): Promise<void>
disconnect(): void
isConnected(): boolean
```

##### Subscriptions
```typescript
subscribe(handler: MessageHandler): () => void
onError(handler: ErrorHandler): () => void
```

##### Commands - Device Information
```typescript
queryDeviceInfo(protocolVersion?: number): void
queryDeviceTime(): void
setDeviceTime(unixTimestamp: number): void
queryBatteryAndStorage(): void
exportPrivateKey(): void
getCustomVars(): void
setCustomVar(name: string, value: string): void
reboot(): void
factoryReset(): void
```

##### Commands - Messaging
```typescript
sendTextMessage(pubKey: Buffer, text: string): void
sendChannelMessage(channelIndex: number, text: string): void
syncNextMessage(): void
```

##### Commands - Contacts
```typescript
getContacts(since?: number): void
```

##### Commands - Configuration
```typescript
setAdvertName(name: string): void
setAdvertLocation(latitude: number, longitude: number, altitude?: number): void
getChannel(channelIndex: number): void
setChannel(channelIndex: number, name: string, secret: Buffer): void
```

##### Commands - Signing
```typescript
signStart(): void
signData(data: Buffer): void
signFinish(): void
```

##### Commands - Generic
```typescript
sendCommand(payload: Buffer): void
```

## Message Types

The `ResponseMessage` union type includes:

### Response Messages
- `{ type: 'ok' }` - Operation successful
- `{ type: 'error'; errorCode: number; errorMessage: string }` - Error response
- `{ type: 'disabled' }` - Feature is disabled
- `{ type: 'noMoreMessages' }` - No more queued messages

### Device Info
- `{ type: 'deviceInfo'; data: DeviceInfo }` - Device information
- `{ type: 'deviceTime'; data: DeviceTime }` - Current device time
- `{ type: 'batteryAndStorage'; data: BatteryAndStorage }` - Battery/storage status

### Contact Management
- `{ type: 'contactsStart'; totalCount: number }` - Start of contacts list
- `{ type: 'contact'; data: Contact }` - Individual contact
- `{ type: 'endOfContacts' }` - End of contacts list

### Messages
- `{ type: 'contactMessage'; data: ContactMessage }` - Message from contact
- `{ type: 'channelMessage'; data: ChannelMessage }` - Message from channel
- `{ type: 'messageWaiting' }` - Push notification: message in queue

### Other
- `{ type: 'sent'; tag?: number; estimatedTimeoutMs?: number; flood?: boolean }` - Message sent confirmation
- `{ type: 'customVars'; vars: Record<string, string> }` - Custom variables
- `{ type: 'privateKey'; key: Buffer }` - Exported private key
- `{ type: 'channelInfo'; data: ChannelInfo }` - Channel information
- `{ type: 'advertPath'; data: AdvertPath }` - Advertisement path
- `{ type: 'signature'; data: SignatureInfo }` - Signature data
- `{ type: 'pathUpdated'; pubKey: Buffer }` - Path updated notification
- `{ type: 'sendConfirmed'; ack: Buffer; tripTimeMs: number }` - Send confirmed
- `{ type: 'signStart'; maxDataLen: number }` - Signature operation started
- `{ type: 'unknown'; code: number; payload: Buffer }` - Unknown message type

## Type Guards

Helper functions for type checking:

```typescript
import { isError, isMessage } from './lib';

radio.subscribe((message) => {
  if (isError(message)) {
    // message.type === 'error'
    // message.errorCode: number
    // message.errorMessage: string
  }
  
  if (isMessage(message)) {
    // message.type === 'contactMessage' || 'channelMessage'
    // message.data: ContactMessage | ChannelMessage
  }
});
```

## Data Structures

### Contact
```typescript
interface Contact {
  id: ContactIdentity;
  name: string;
  type: number;
  flags: number;
  outPathLen: number;
  outPath: Buffer; // MAX_PATH_SIZE bytes
  lastAdvertTimestamp: number;
  lastModified: number;
  gpsLat?: number; // optional, 6 decimal places
  gpsLon?: number; // optional, 6 decimal places
}
```

### ContactMessage / ChannelMessage
```typescript
interface ContactMessage {
  type: 'contact';
  fromKeyPrefix: Buffer; // 6 bytes
  pathLen: number;
  textType: number;
  timestamp: number;
  text: string;
  snrDb?: number; // v3+ only
}

interface ChannelMessage {
  type: 'channel';
  channelIndex: number;
  pathLen: number;
  textType: number;
  timestamp: number;
  text: string;
  snrDb?: number; // v3+ only
}
```

### DeviceInfo
```typescript
interface DeviceInfo {
  firmwareVersion: string;
  firmwareCode: number;
  buildDate: string;
  manufacturerName: string;
  maxContacts: number;
  maxGroupChannels: number;
  blePin: number;
}
```

## Examples

### Example 1: Query Device Info

```typescript
import { RadioClient } from './lib';

const radio = new RadioClient();
await radio.connect('/dev/ttyUSB0');

radio.subscribe((message) => {
  if (message.type === 'deviceInfo') {
    console.log('Device Info:');
    console.log(`  Firmware: ${message.data.firmwareVersion}`);
    console.log(`  Build: ${message.data.buildDate}`);
    console.log(`  Manufacturer: ${message.data.manufacturerName}`);
  }
});

radio.queryDeviceInfo();
```

### Example 2: Send a Message

```typescript
import { RadioClient } from './lib';

const radio = new RadioClient();
await radio.connect('/dev/ttyUSB0');

// Send message to a contact
const recipientKey = Buffer.from('...32 bytes...', 'hex');
radio.sendTextMessage(recipientKey, 'Hello, World!');

// Wait for confirmation
radio.subscribe((message) => {
  if (message.type === 'sent') {
    console.log(`Message sent! (tag: ${message.tag})`);
  }
});
```

### Example 3: Monitor All Incoming Messages

```typescript
import { RadioClient, ResponseMessage } from './lib';

const radio = new RadioClient();
await radio.connect('/dev/ttyUSB0');

let messageCount = 0;

radio.subscribe((message: ResponseMessage) => {
  messageCount++;
  console.log(`[${messageCount}] Received: ${message.type}`);
  
  if (message.type === 'contactMessage') {
    console.log(`  From: ${message.data.fromKeyPrefix.toString('hex')}`);
    console.log(`  Text: ${message.data.text}`);
  }
});

// Request to sync messages
radio.syncNextMessage();
```

### Example 4: Get Contacts List

```typescript
import { RadioClient } from './lib';

const radio = new RadioClient();
await radio.connect('/dev/ttyUSB0');

const contacts = [];

radio.subscribe((message) => {
  if (message.type === 'contact') {
    contacts.push({
      name: message.data.name,
      key: message.data.id.pubKey,
    });
    console.log(`Added contact: ${message.data.name}`);
  } else if (message.type === 'endOfContacts') {
    console.log(`Total contacts: ${contacts.length}`);
  }
});

radio.getContacts();
```

### Example 5: Error Handling

```typescript
import { RadioClient, isError } from './lib';

const radio = new RadioClient();
await radio.connect('/dev/ttyUSB0');

radio.subscribe((message) => {
  if (isError(message)) {
    console.error(`Error (${message.errorCode}): ${message.errorMessage}`);
  }
});

radio.onError((error: Error) => {
  console.error(`Connection error: ${error.message}`);
});
```

## Constants

All protocol constants are exported from `lib/types.ts`:

```typescript
// Sizes
PUB_KEY_SIZE           // 32
SHARED_SECRET_SIZE     // 32
MAX_PATH_SIZE          // 32
SIGNATURE_SIZE         // 64
MAX_SIGN_DATA_LEN      // 8192
MAX_FRAME_SIZE         // 172
DEFAULT_BAUD_RATE      // 115200

// Enums
CommandCode            // All command codes
ResponseCode           // All response codes
PushCode               // All push codes
ErrorCode              // All error codes
TextType               // Message text types
AdvertType             // Advertisement types
```

## Protocol Details

### Frame Format
Each frame sent to/from the radio follows this format:
```
[Header (1 byte)] [Length LSB] [Length MSB] [Payload (0-172 bytes)]
  '<' or '>'        payload_len payload_len >> 8
```

### Communication Flow
1. Client sends command frame
2. Radio processes command
3. Radio sends response frame(s)
4. Radio may send push messages at any time

### Message Subscription Model
- Multiple subscribers can be registered
- All subscribers receive all messages
- Subscribers are called synchronously
- Use `subscribe()` to register, it returns an unsubscribe function

## Error Handling

The library provides comprehensive error handling:

```typescript
radio.onError((error: Error) => {
  // Handle serial port errors, frame parsing errors, etc.
  console.error(`Error: ${error.message}`);
});

radio.subscribe((message) => {
  if (message.type === 'error') {
    // Handle radio-side errors
    console.error(`Radio error: ${message.errorMessage}`);
  }
});
```

## Performance Considerations

- The frame parser is efficient and processes data incrementally
- No deep copies are made for frame payloads
- Message handlers are called synchronously, keep them fast
- Large bulk operations (like getting many contacts) may take time

## See Also

- `examples/companion_radio/MyMesh.cpp` - Firmware implementation
- `USB_VARIANT_README.md` - USB variant setup guide
- Original `index.ts` - Legacy simple version for reference
