# Message Queue Synchronization Feature

## Overview

The logger now automatically retrieves all queued messages from the radio before resuming normal operations. This ensures you don't miss any messages that arrived while the logger wasn't connected.

## How It Works

### Startup Sequence

1. **Connect to radio** - Establishes USB/serial connection
2. **Wait for device** - Gives radio time to initialize (500ms)
3. **Setup message subscription** - Registers the main message handler
4. **Synchronize queued messages** - Retrieves all waiting messages
5. **Subscribe to channels** - Sets up Public and hashtag channels
6. **Start listening** - Displays fresh messages as they arrive

### Queue Synchronization Logic

When the logger starts, it enters "queue sync mode":

```typescript
// During queue sync:
- isSyncingQueue = true
- messageCount tracking is enabled
- Each message triggers syncNextMessage() to get the next one
- When noMoreMessages arrives, exit queue sync mode
- Display count of messages retrieved
- Return to normal operation
```

The synchronization happens **silently** - queued messages are counted but not displayed on the terminal. This keeps the startup output clean.

### Safety Mechanisms

1. **Timeout between attempts** - 2 seconds between syncNextMessage() calls
2. **Maximum attempts** - Up to 3 attempts to retrieve messages
3. **Absolute timeout** - 60 second safety limit
4. **Graceful fallback** - If queue sync times out, continues with normal operation

## Example Output

### With queued messages:
```
=== MeshCore Radio Logger ===

Connecting to /dev/ttyACM0...
Using port: /dev/ttyACM0

Setting up message handlers...

📥 Retrieving queued messages...
✅ Retrieved 5 queued messages

Subscribing to channels...
Listening for messages (press Ctrl+C to exit)...
```

### With empty queue:
```
=== MeshCore Radio Logger ===

Connecting to /dev/ttyACM0...
Using port: /dev/ttyACM0

Setting up message handlers...

📥 Retrieving queued messages...
✅ Message queue is empty

Subscribing to channels...
Listening for messages (press Ctrl+C to exit)...
```

## Code Structure

### `syncAllQueuedMessages()` Function

- **Purpose**: Retrieve all queued messages from the radio
- **Parameters**:
  - `radio`: RadioClient instance
  - `setSyncComplete`: Callback to set the noMoreMessages handler
- **Returns**: Promise that resolves when done
- **Behavior**:
  - Attempts to sync messages with retry logic
  - Resolves when `noMoreMessages` received or timeout
  - Provides 60-second safety timeout

### Main Subscription Handler

Modified to handle queue sync mode:

```typescript
if (isSyncingQueue) {
  // In sync mode: count and fetch silently
  if (message.type === 'noMoreMessages') {
    // Signal completion
    if (queueSyncComplete) {
      queueSyncComplete();
    }
  } else if (!isError(message)) {
    messageCount++;
    radio.syncNextMessage(); // Request next
  }
  return; // Don't display
}

// Normal mode: display all messages
// ... normal message handling ...
```

## Benefits

1. **No missed messages** - All queued messages are retrieved before normal operation
2. **Clean startup** - Queue sync happens silently, then you see fresh messages
3. **Transparent** - Message count is shown so you know what was queued
4. **Robust** - Multiple timeout mechanisms prevent getting stuck

## Testing

To test queue sync:

1. **Send messages while disconnected**
   - Leave a few unsent/unread messages on the radio
   - Disconnect the logger (Ctrl+C)
   - Send new messages to that radio

2. **Reconnect and observe**
   - Run the logger again
   - It should display:
     ```
     📥 Retrieving queued messages...
     ✅ Retrieved X queued messages
     ```
   - Then display fresh messages normally

3. **Verify message count**
   - Send exactly 3 messages and disconnect
   - Reconnect and verify you see "Retrieved 3 queued messages"

## Future Enhancements

Possible improvements:

1. **Option to display queued messages** - Add verbose flag to show each queued message during sync
2. **Persistent message log** - Save queued messages to file for history
3. **Progress indicator** - Show "Retrieving message 5/10..." for large queues
4. **Adjustable timeout** - Allow config of sync duration
5. **Skip sync option** - Command-line flag to skip queue sync if needed

## Troubleshooting

### Messages still showing 0 queued but I sent messages

- Check if messages were actually stored on the radio
- Some messages may expire if timestamp is too old
- Verify the radio has packet logging enabled (messages appear in logRxData)

### Queue sync times out

- Normal if there are many queued messages
- Logger continues with normal operation after timeout
- Try running again - additional calls to syncNextMessage() will get remaining messages

### Queued messages don't display

- They're intentionally suppressed during sync to keep startup clean
- Once sync completes, messages display normally
- Count is shown: "✅ Retrieved X queued messages"
