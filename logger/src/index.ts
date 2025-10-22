/**
 * MeshCore Radio Logger Example
 * Demonstrates usage of the RadioClient library with message subscriptions
 */

import { RadioClient, ResponseMessage, isError, isMessage } from './lib';

/**
 * Example application that connects to a radio and listens to messages
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

    // Set up message subscription
    console.log('Setting up message handlers...\n');

    let contactCount = 0;
    let messageCount = 0;

    radio.subscribe((message: ResponseMessage) => {
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
            console.log(`\n📢 Channel Message #${messageCount}:`);
            console.log(`   Channel: ${msg.channelIndex}`);
            console.log(`   Time: ${new Date(msg.timestamp * 1000).toISOString()}`);
            if (msg.snrDb) {
              console.log(`   SNR: ${msg.snrDb.toFixed(1)}dB`);
            }
            console.log(`   Text: ${msg.text}`);
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
          console.log('⏰ Message waiting notification\n');
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

    // Query device info
    console.log('Querying device information...');
    radio.queryDeviceInfo();

    // Wait for response
    await new Promise(resolve => setTimeout(resolve, 1000));

    // Demonstrate additional commands
    console.log('Querying battery and storage...');
    radio.queryBatteryAndStorage();

    await new Promise(resolve => setTimeout(resolve, 1000));

    console.log('Requesting contacts list...');
    radio.getContacts();

    // Wait for messages to arrive
    await new Promise(resolve => setTimeout(resolve, 3000));

    console.log('\n--- Command Examples ---');
    console.log('You can interact with the radio using methods like:');
    console.log('  radio.sendTextMessage(pubKey, "text")');
    console.log('  radio.setAdvertName("My Device")');
    console.log('  radio.setAdvertLocation(latitude, longitude)');
    console.log('  radio.queryDeviceTime()');
    console.log('  radio.setDeviceTime(unixTimestamp)');
    console.log('  radio.getCustomVars()');
    console.log('');

    // Keep the program running to receive messages
    console.log('Listening for messages (press Ctrl+C to exit)...\n');
    await new Promise(() => {
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
