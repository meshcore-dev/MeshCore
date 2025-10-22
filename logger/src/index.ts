/**
 * MeshCore Radio Logger Example
 * Demonstrates usage of the RadioClient library with message subscriptions
 */

import { RadioClient, ResponseMessage, isError, isMessage } from './lib';

/**
 * Format and display contact/advert information
 */
function displayContactInfo(contact: any, title: string = '📍 Contact Advert'): void {
  console.log(`\n${title}:`);
  
  if (contact.name) {
    console.log(`   Name: ${contact.name}`);
  }
  
  console.log(`   Public Key: ${contact.id?.pubKey?.toString('hex') || contact.pubKey?.toString('hex')}`);
  
  if (contact.gpsLat !== undefined && contact.gpsLon !== undefined) {
    const lat = contact.gpsLat / 1e6;
    const lon = contact.gpsLon / 1e6;
    console.log(`   Location: ${lat.toFixed(6)}, ${lon.toFixed(6)}`);
  }
  
  console.log();
}

/**
 * Extract the path from a raw wire packet
 * Packet format: [header][optional transport codes (4)][path_len][path...][payload...]
 * Returns the path as a hex string, or null if unable to extract
 */
function extractPathFromWirePacket(rawPacket: Buffer): string | null {
  if (!rawPacket || rawPacket.length < 2) return null;

  let offset = 0;

  // Read header byte
  const header = rawPacket[offset++];

  // Check if packet has transport codes (ROUTE_TYPE is bits 0-1)
  const routeType = header & 0x03;
  const hasTransportCodes =
    routeType === 0x00 || // ROUTE_TYPE_TRANSPORT_FLOOD
    routeType === 0x03; // ROUTE_TYPE_TRANSPORT_DIRECT

  if (hasTransportCodes) {
    // Skip 4 bytes of transport codes
    offset += 4;
  }

  // Read path length
  if (offset >= rawPacket.length) return null;
  const pathLen = rawPacket[offset++];

  // Validate path length
  if (pathLen > 32 || offset + pathLen > rawPacket.length) return null;

  // Extract and convert path to hex string
  if (pathLen > 0) {
    const path = rawPacket.subarray(offset, offset + pathLen);
    return path.toString('hex').toUpperCase();
  }

  return '';
}

/**
 * Synchronize all queued messages from the radio
 * Messages are retrieved until the radio reports "noMoreMessages"
 * This empties the message queue before resuming normal operations
 */
async function syncAllQueuedMessages(
  radio: RadioClient,
  setSyncComplete: (callback: () => void) => void
): Promise<void> {
  return new Promise((resolve) => {
    let attemptCount = 0;
    const maxAttempts = 3;

    const attemptSync = () => {
      if (attemptCount >= maxAttempts) {
        resolve();
        return;
      }

      attemptCount++;
      radio.syncNextMessage();

      // Wait for next message or timeout
      setTimeout(() => {
        attemptSync();
      }, 2000);
    };

    // Set the callback that will be called when noMoreMessages arrives
    setSyncComplete(resolve);

    // Start the retrieval process
    attemptSync();

    // Absolute timeout safety
    setTimeout(() => {
      resolve();
    }, 60000);
  });
}

/**
 * Example application that connects to a radio and listens to messages
 * 
 * NOTE ON CHANNEL MESSAGES:
 * Channel messages arrive in two ways:
 * 1. As decoded channelMessage responses (type='channelMessage') - when properly subscribed
 * 2. As raw packets in logRxData (type='logRxData') - when packet logging is enabled on the firmware
 * 
 * For the Public channel and hashtag channels to work, the firmware must:
 * - Support channel subscription storage
 * - Decrypt and deliver channel messages via the normal message dispatch path
 * 
 * Currently, all messages appear as logRxData when packet logging is enabled in firmware.
 */
async function main() {
  const args = process.argv.slice(2);
  const portPath = args[0];

  try {
    console.log('=== MeshCore Radio Logger ===\n');

    // Create client instance
    const radio = new RadioClient();

    // Connect to radio (auto-detect if no port specified)
    if (portPath) {
      console.log(`Connecting to ${portPath}...`);
      await radio.connect(portPath);
    } else {
      console.log('Auto-detecting radio port...');
      const ports = await RadioClient.findAvailablePorts();
      if (ports.length === 0) {
        throw new Error('No serial ports found');
      }
      const port = ports[0].path;
      console.log(`Using port: ${port}\n`);
      await radio.connect(port);
    }

    // Wait a bit for the device to be ready
    await new Promise(resolve => setTimeout(resolve, 500));

    // Set up state for tracking queue sync
    let isSyncingQueue = false;
    let messageCount = 0;
    let contactCount = 0;

    // Cache to track recent messages and their paths
    interface MessageCacheEntry {
      timestamp: number;
      path: string;
      senderName?: string;
    }
    const messageCache = new Map<number, MessageCacheEntry>(); // timestamp -> message info
    const contactsByName = new Map<string, any>(); // name -> contact info
    let queueSyncComplete: (() => void) | undefined;
    let aquaTestChannelIndex: number | undefined; // Will be set during channel subscription

    // Set up message subscription
    console.log('Setting up message handlers...\n');

    radio.subscribe((message: ResponseMessage) => {
      // During queue sync, suppress normal output and auto-request next message
      if (isSyncingQueue) {
        if (message.type === 'noMoreMessages') {
          // Queue sync is complete
          if (queueSyncComplete) {
            queueSyncComplete();
          }
        } else if (!isError(message)) {
          messageCount++;
          // Request next message
          radio.syncNextMessage();
        }
        return;
      }

      // Debug: Log raw message as hexadecimal
      if (message.rawPayload) {
        const payloadLen = message.rawPayload.length;
        console.log(`[DEBUG] Received message [${message.type}]: ${payloadLen} bytes`);
        console.log(`[DEBUG] Hex: ${message.rawPayload.toString('hex')}`);
        
        // For unknown messages, show some parsed data
        if (message.type === 'unknown') {
          console.log(`[DEBUG] Code: 0x${message.code.toString(16).padStart(2, '0')}`);
          // Show first 16 bytes as hex with interpretation
          if (payloadLen >= 4) {
            const firstBytes = message.rawPayload.subarray(0, Math.min(16, payloadLen));
            console.log(`[DEBUG] First 16 bytes: ${firstBytes.toString('hex')}`);
          }
        }
      }

      // Handle different message types
      if (isError(message)) {
        console.error(`❌ Error: ${message.errorMessage} (code: ${message.errorCode})`);
        return;
      }

      switch (message.type) {
        case 'deviceInfo':
          console.log('\n📱 Device Information:');
          console.log(`   Firmware: ${message.data.firmwareVersion} (code ${message.data.firmwareCode})`);
          console.log(`   Build Date: ${message.data.buildDate}`);
          console.log(`   Manufacturer: ${message.data.manufacturerName}`);
          console.log(`   Max Contacts: ${message.data.maxContacts}`);
          console.log(`   Max Channels: ${message.data.maxGroupChannels}`);
          console.log(`   BLE PIN: ${message.data.blePin === 0 ? '(disabled)' : message.data.blePin}\n`);
          break;

        case 'batteryAndStorage':
          console.log(`🔋 Battery: ${message.data.batteryMillivolts}mV`);
          console.log(`💾 Storage: ${message.data.storageUsedKb}KB / ${message.data.storageTotalKb}KB\n`);
          break;

        case 'contactsStart':
          contactCount = 0;
          console.log(`📥 Contacts list starting (${message.totalCount} total)...`);
          break;

        case 'contact':
          contactCount++;
          const contact = message.data;
          contactsByName.set(contact.name, contact); // Store for later lookup
          console.log(`   ${contactCount}. ${contact.name} (type: ${contact.type})`);
          console.log(`      Key: ${contact.id.pubKey.subarray(0, 6).toString('hex')}...`);
          if (contact.gpsLat !== undefined && contact.gpsLon !== undefined) {
            console.log(`      Location: ${contact.gpsLat / 1e6}, ${contact.gpsLon / 1e6}`);
          }
          break;

        case 'endOfContacts':
          console.log(`✅ Contacts list complete (${contactCount} contacts)\n`);
          break;

        case 'contactMessage':
          messageCount++;
          if (isMessage(message)) {
            const msg = message.data;
            console.log(`\n💬 Contact Message #${messageCount}:`);
            console.log(`   From: ${msg.fromKeyPrefix.toString('hex')}`);
            console.log(`   Time: ${new Date(msg.timestamp * 1000).toISOString()}`);
            if (msg.snrDb) {
              console.log(`   SNR: ${msg.snrDb.toFixed(1)}dB`);
            }
            console.log(`   Text: ${msg.text}`);
          }
          break;

        case 'channelMessage':
          messageCount++;
          if (isMessage(message)) {
            const msg = message.data;
            const channelName = radio.getChannelName(msg.channelIndex);
            const channelDisplay = channelName
              ? `${msg.channelIndex} (${channelName})`
              : msg.channelIndex.toString();
            console.log(`\n📢 Channel Message #${messageCount}:`);
            console.log(`   Channel: ${channelDisplay}`);
            console.log(`   Time: ${new Date(msg.timestamp * 1000).toISOString()}`);
            if (msg.snrDb) {
              console.log(`   SNR: ${msg.snrDb.toFixed(1)}dB`);
            }
            console.log(`   Text: ${msg.text}`);

            // Auto-respond to test messages on #aqua-test channel
            if (msg.channelIndex === aquaTestChannelIndex && msg.text.toLowerCase().includes('test')) {
              // Extract sender name from "name: message" format
              const colonIndex = msg.text.indexOf(':');
              const senderName = colonIndex > 0 ? msg.text.substring(0, colonIndex).trim() : 'Unknown';

              // Find cached path for this message - get the most recent cached path
              // (logRxData typically arrives just before channelMessage with the routing info)
              let cachedPath = '(unknown)';
              let mostRecentTimestamp = 0;
              let mostRecentPath = '';
              
              // Find the most recent cached message (within last 10 seconds)
              for (const [timestamp, entry] of messageCache.entries()) {
                if (timestamp > mostRecentTimestamp && timestamp > Math.floor(Date.now() / 1000) - 10) {
                  mostRecentTimestamp = timestamp;
                  mostRecentPath = entry.path;
                }
              }
              
              if (mostRecentPath) {
                // Convert empty path to "direct" (no intermediate nodes)
                cachedPath = mostRecentPath === '(empty)' ? 'direct' : mostRecentPath;
              }

              // Send response with sender name and path
              const response = `Got it ${senderName} | Path: ${cachedPath}`;
              
              // Delay to allow current message processing to complete before sending response
              // This prevents protocol conflicts with the radio
              setTimeout(() => {
                try {
                  if (aquaTestChannelIndex !== undefined) {
                    radio.sendChannelMessage(aquaTestChannelIndex, response);
                    console.log(`   ✉️  Auto-Response Sent: ${response}`);
                  }
                } catch (err) {
                  console.error(`   ⚠️  Failed to send response: ${err instanceof Error ? err.message : err}`);
                }
              }, 500);
            }
          }
          break;

        case 'deviceTime':
          console.log(`🕐 Device Time: ${new Date(message.data.timestamp * 1000).toISOString()}\n`);
          break;

        case 'customVars':
          console.log('📋 Custom Variables:');
          for (const [key, value] of Object.entries(message.vars)) {
            console.log(`   ${key}: ${value}`);
          }
          console.log();
          break;

        case 'sent':
          console.log(`✉️  Message sent (tag: ${message.tag}, timeout: ${message.estimatedTimeoutMs}ms, flood: ${message.flood})\n`);
          break;

        case 'messageWaiting':
          console.log('⏰ Message waiting notification');
          console.log('   Fetching next message...\n');
          radio.syncNextMessage();
          break;

        case 'ok':
          console.log('✅ OK\n');
          break;

        case 'noMoreMessages':
          console.log('📭 No more messages\n');
          break;

        case 'disabled':
          console.log('⛔ Feature disabled\n');
          break;

        case 'logRxData':
          console.log(`📡 Raw RX Packet Log:`);
          console.log(`   SNR: ${message.snr.toFixed(2)} dB`);
          console.log(`   RSSI: ${message.rssi} dBm`);
          
          // Parse the packet header to show some info
          if (message.rawPacket.length >= 1) {
            const header = message.rawPacket[0];
            const routeType = header & 0x03;
            const payloadType = (header >> 2) & 0x0F;
            const version = (header >> 6) & 0x03;
            
            const routeTypeNames = ['TRANSPORT_FLOOD', 'FLOOD', 'DIRECT', 'TRANSPORT_DIRECT'];
            const payloadTypeNames = [
              'REQ', 'RESPONSE', 'TXT_MSG', 'ACK', 'ADVERT', 'GRP_TXT', 'GRP_DATA',
              'ANON_REQ', 'PATH', 'TRACE', 'MULTIPART', 'RESERVED_B', 'RESERVED_C',
              'RESERVED_D', 'RESERVED_E', 'RAW_CUSTOM'
            ];
            
            console.log(`   📦 Packet Header: 0x${header.toString(16).padStart(2, '0')}`);
            console.log(`       Route: ${routeType} (${routeTypeNames[routeType]})`);
            console.log(`       Type: ${payloadType} (${payloadTypeNames[payloadType]})`);
            console.log(`       Ver: ${version}`);
            
            // If there's payload length info, try to show it
            if (message.rawPacket.length >= 3) {
              const payloadLen = message.rawPacket.readUInt16LE(1);
              console.log(`       Payload Length: ${payloadLen}`);
            }
            
            // Extract and display the path
            const path = extractPathFromWirePacket(message.rawPacket);
            if (path !== null) {
              console.log(`       Path: ${path || '(empty)'}`);
              
              // For GRP_TXT messages, cache the path info for later use
              if (payloadType === 5) { // PAYLOAD_TYPE_GRP_TXT
                // Store path by Unix timestamp (seconds) for later lookup
                const cacheKey = Math.floor(Date.now() / 1000);
                messageCache.set(cacheKey, {
                  timestamp: cacheKey,
                  path: path || '(empty)'
                });
              }
            }
            
            // For GRP_TXT (group text), show the channel and content
            if (payloadType === 5) { // PAYLOAD_TYPE_GRP_TXT
              console.log(`       ℹ️  This is a GROUP TEXT MESSAGE (typical for channels)`);
              if (message.rawPacket.length >= 4) {
                const channelHashByte = message.rawPacket[3];
                console.log(`       Channel Hash Byte: 0x${channelHashByte.toString(16).padStart(2, '0')}`);
              }
            }
          }
          
          console.log(`   Packet: ${message.rawPacket.toString('hex')} (${message.rawPacket.length} bytes)\n`);
          break;

        case 'contact':
          contactsByName.set(message.data.name, message.data); // Store for later lookup
          displayContactInfo(message.data, '📍 Contact Advert');
          break;

        case 'simpleAdvert':
          console.log(`\n📍 Simple Advert (pubkey only):`);
          console.log(`   Public Key: ${message.pubKey.toString('hex')}`);
          console.log(`   (To get name and GPS, enable auto-add in radio settings)\n`);
          break;

        case 'unknown':
          console.log(`❓ Unknown message type: 0x${message.code.toString(16)} (${message.payload.length} bytes)\n`);
          break;

        default:
          console.log(`ℹ️  ${message.type}\n`);
      }
    });

    // Set up error handler
    radio.onError((error: Error) => {
      console.error('❌ Radio error:', error.message);
    });

    // Retrieve all queued messages before starting normal operation
    console.log('\n📥 Retrieving queued messages...');
    isSyncingQueue = true;
    let queuedMessageCount = 0;
    const initialMessageCount = messageCount;
    await syncAllQueuedMessages(radio, (completeCallback) => {
      queueSyncComplete = completeCallback;
    });
    queuedMessageCount = messageCount - initialMessageCount;
    isSyncingQueue = false;
    if (queuedMessageCount > 0) {
      console.log(`✅ Retrieved ${queuedMessageCount} queued messages\n`);
    } else {
      console.log('✅ Message queue is empty\n');
    }

    // Query device info
    // console.log('Querying device information...');
    // radio.queryDeviceInfo();

    // Wait for response
    // await new Promise(resolve => setTimeout(resolve, 1000));

    // Demonstrate additional commands
    // console.log('Querying battery and storage...');
    // radio.queryBatteryAndStorage();

    // await new Promise(resolve => setTimeout(resolve, 1000));

    // console.log('Requesting contacts list...');
    // radio.getContacts();

    // await new Promise(resolve => setTimeout(resolve, 1000);

    console.log('Subscribing to channels...');
    // Subscribe to the Public channel (channel 0)
    // The Public channel uses a well-known pre-shared key
    // PSK: "izOH6cXN6mrJ5e26oRXNcg==" (Base64-encoded)
    // When decoded, this is 16 bytes for AES-128 encryption
    const publicChannelPSK = Buffer.from('izOH6cXN6mrJ5e26oRXNcg==', 'base64');
    radio.setChannel(0, 'Public', publicChannelPSK);
    
    // Subscribe to the #aqua-test hash channel
    aquaTestChannelIndex = 1; // Store the channel index for later use in message handler
    radio.subscribeHashChannel('#aqua-test', aquaTestChannelIndex);

    // Enable automatic addition of discovered contacts
    radio.setAutoAddContacts(true);

    // Wait for messages to arrive
    // await new Promise(resolve => setTimeout(resolve, 3000));

    // console.log('\n--- Command Examples ---');
    // console.log('You can interact with the radio using methods like:');
    // console.log('  radio.sendTextMessage(pubKey, "text")');
    // console.log('  radio.setAdvertName("My Device")');
    // console.log('  radio.setAdvertLocation(latitude, longitude)');
    // console.log('  radio.queryDeviceTime()');
    // console.log('  radio.setDeviceTime(unixTimestamp)');
    // console.log('  radio.getCustomVars()');
    // console.log('');

    // Keep the program running to receive messages
    console.log('Listening for messages (press Ctrl+C to exit)...\n');
    await new Promise((resolve) => {
      /* never resolves */
    });
  } catch (error) {
    console.error(
      'Error:',
      error instanceof Error ? error.message : error
    );
    process.exit(1);
  }
}

main().catch(console.error);

// Handle clean shutdown
process.on('SIGINT', () => {
  console.log('\n\nShutting down...');
  process.exit(0);
});
