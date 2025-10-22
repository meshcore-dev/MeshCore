/**
 * Advanced Usage Examples for RadioClient Library
 * These examples show various ways to use the library effectively
 */

import { RadioClient, ResponseMessage, isError, isMessage, Contact } from './lib';

// ============================================================================
// EXAMPLE 1: Monitoring Messages with Filtering
// ============================================================================

async function example_MonitorMessagesWithFiltering() {
  const radio = new RadioClient();
  await radio.connect('/dev/ttyUSB0');

  const textMessages: string[] = [];
  const errors: Array<{ code: number; message: string }> = [];

  radio.subscribe((msg: ResponseMessage) => {
    if (isError(msg)) {
      errors.push({ code: msg.errorCode, message: msg.errorMessage });
    }

    if (isMessage(msg)) {
      textMessages.push(msg.data.text);
    }
  });

  // Listen for 5 seconds
  await new Promise(resolve => setTimeout(resolve, 5000));

  console.log(`Received ${textMessages.length} messages`);
  console.log(`Encountered ${errors.length} errors`);
  
  radio.disconnect();
}

// ============================================================================
// EXAMPLE 2: Request-Response Pattern
// ============================================================================

async function example_RequestResponse(): Promise<Contact[]> {
  const radio = new RadioClient();
  await radio.connect('/dev/ttyACM0');

  const contacts: Contact[] = [];
  let done = false;

  radio.subscribe((msg: ResponseMessage) => {
    if (msg.type === 'contact') {
      contacts.push(msg.data);
    } else if (msg.type === 'endOfContacts') {
      done = true;
    }
  });

  // Request contacts
  radio.getContacts();

  // Wait for completion with timeout
  let waited = 0;
  while (!done && waited < 10000) {
    await new Promise(resolve => setTimeout(resolve, 100));
    waited += 100;
  }
  console.log(`Retrieved ${contacts.length} contacts`);
  console.log(`Waited ${waited} ms for contacts retrieval`);
  console.log(contacts);

  radio.disconnect();
  return contacts;
}

// ============================================================================
// EXAMPLE 3: Async Command with Promise Wrapper
// ============================================================================

class RadioClientAsync extends RadioClient {
  async queryDeviceInfoAsync(): Promise<any> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('Device info query timeout'));
      }, 5000);

      const unsubscribe = (this as any).subscribe((msg: ResponseMessage) => {
        if (msg.type === 'deviceInfo') {
          clearTimeout(timeout);
          unsubscribe();
          resolve(msg.data);
        }
      });

      (this as any).queryDeviceInfo();
    });
  }
}

// ============================================================================
// EXAMPLE 4: Batch Processing - Get Contacts and Battery
// ============================================================================

async function example_BatchQueries() {
  const radio = new RadioClient();
  await radio.connect('/dev/ttyUSB0');

  const results = {
    deviceInfo: null as any,
    battery: null as any,
  };

  radio.subscribe((msg: ResponseMessage) => {
    if (msg.type === 'deviceInfo') {
      results.deviceInfo = msg.data;
    } else if (msg.type === 'batteryAndStorage') {
      results.battery = msg.data;
    }
  });

  // Send multiple queries quickly
  radio.queryDeviceInfo();
  radio.queryBatteryAndStorage();

  // Wait for both responses
  let retries = 0;
  while ((!results.deviceInfo || !results.battery) && retries < 100) {
    await new Promise(resolve => setTimeout(resolve, 50));
    retries++;
  }

  console.log('Device:', results.deviceInfo?.firmwareVersion);
  console.log('Battery:', results.battery?.batteryMillivolts, 'mV');

  radio.disconnect();
}

// ============================================================================
// EXAMPLE 5: Real-time Message Statistics
// ============================================================================

async function example_MessageStatistics() {
  const radio = new RadioClient();
  await radio.connect('/dev/ttyUSB0');

  const stats = {
    totalMessages: 0,
    byType: {} as Record<string, number>,
    bySource: {} as Record<string, number>,
    errors: 0,
  };

  radio.subscribe((msg: ResponseMessage) => {
    stats.totalMessages++;

    // Count by message type
    stats.byType[msg.type] = (stats.byType[msg.type] || 0) + 1;

    // Count errors
    if (msg.type === 'error') {
      stats.errors++;
    }

    // Count by source (for messages)
    if (isMessage(msg)) {
      const source = msg.data.type === 'contact' 
        ? msg.data.fromKeyPrefix.toString('hex')
        : `Channel ${msg.data.channelIndex}`;
      stats.bySource[source] = (stats.bySource[source] || 0) + 1;
    }
  });

  // Report statistics every 10 seconds
  const reportInterval = setInterval(() => {
    console.log('\n--- Message Statistics ---');
    console.log(`Total: ${stats.totalMessages}`);
    console.log(`Errors: ${stats.errors}`);
    console.log('By type:', stats.byType);
    console.log('By source:', stats.bySource);
  }, 10000);

  // Listen for 1 minute
  await new Promise(resolve => setTimeout(resolve, 60000));

  clearInterval(reportInterval);
  radio.disconnect();
}

// ============================================================================
// EXAMPLE 6: Setting Device Configuration
// ============================================================================

async function example_DeviceConfiguration() {
  const radio = new RadioClient();
  await radio.connect('/dev/ttyUSB0');

  // Track operation results
  const results: Record<string, string> = {};

  radio.subscribe((msg: ResponseMessage) => {
    if (msg.type === 'ok') {
      results.lastOp = 'success';
    } else if (msg.type === 'error') {
      results.lastOp = `error: ${msg.errorMessage}`;
    }
  });

  // Set multiple configuration values
  const operations = [
    { name: 'name', fn: () => radio.setAdvertName('MyRadio') },
    { name: 'location', fn: () => radio.setAdvertLocation(40.7128, -74.0060) },
    { name: 'time', fn: () => radio.setDeviceTime(Math.floor(Date.now() / 1000)) },
  ];

  for (const op of operations) {
    results.lastOp = 'pending';
    op.fn();
    
    // Wait for response
    let waited = 0;
    while (results.lastOp === 'pending' && waited < 2000) {
      await new Promise(resolve => setTimeout(resolve, 50));
      waited += 50;
    }

    console.log(`${op.name}: ${results.lastOp}`);
  }

  radio.disconnect();
}

// ============================================================================
// EXAMPLE 7: Message Routing - Send to Different Handlers
// ============================================================================

async function example_MessageRouting() {
  const radio = new RadioClient();
  await radio.connect('/dev/ttyUSB0');

  // Route different message types to different handlers
  const handlers: Record<string, (msg: ResponseMessage) => void> = {
    device: (msg: ResponseMessage) => {
      if (msg.type === 'deviceInfo') {
        console.log('📱 Device:', msg.data.firmwareVersion);
      }
    },
    messages: (msg: ResponseMessage) => {
      if (isMessage(msg)) {
        console.log('💬 Message received');
      }
    },
    errors: (msg: ResponseMessage) => {
      if (isError(msg)) {
        console.error('❌ Error:', msg.errorMessage);
      }
    },
  };

  radio.subscribe((msg: ResponseMessage) => {
    for (const handler of Object.values(handlers)) {
      try {
        handler(msg);
      } catch (e) {
        console.error('Handler error:', e);
      }
    }
  });

  // Demonstrate routing
  radio.queryDeviceInfo();
  radio.syncNextMessage();

  await new Promise(resolve => setTimeout(resolve, 5000));
  radio.disconnect();
}

// ============================================================================
// EXAMPLE 8: Contact Resolver - Map Key Prefixes to Names
// ============================================================================

class ContactResolver {
  private contacts: Map<string, string> = new Map();
  private radio: RadioClient;

  constructor(radio: RadioClient) {
    this.radio = radio;
    this.loadContacts();
  }

  private loadContacts() {
    this.radio.subscribe((msg: ResponseMessage) => {
      if (msg.type === 'contact') {
        // Store mapping of key prefix to name
        const prefix = msg.data.id.pubKey.subarray(0, 6).toString('hex');
        this.contacts.set(prefix, msg.data.name);
      }
    });
  }

  getContactName(keyPrefix: Buffer): string {
    const hex = keyPrefix.toString('hex');
    return this.contacts.get(hex) || `Unknown (${hex})`;
  }

  async resolveAllContacts(): Promise<Map<string, string>> {
    return new Promise((resolve) => {
      const unsubscribe = this.radio.subscribe((msg: ResponseMessage) => {
        if (msg.type === 'endOfContacts') {
          unsubscribe();
          resolve(new Map(this.contacts));
        }
      });

      this.radio.getContacts();
    });
  }
}

// ============================================================================
// EXAMPLE 9: Automatic Reconnection
// ============================================================================

async function example_AutoReconnect() {
  let radio = new RadioClient();
  const portPath = '/dev/ttyUSB0';
  let isConnected = false;

  const connect = async () => {
    try {
      await radio.connect(portPath);
      isConnected = true;
      console.log('Connected');
    } catch (e) {
      console.error('Connection failed:', (e as Error).message);
      isConnected = false;
    }
  };

  radio.onError((error: Error) => {
    console.error('Connection error:', error.message);
    isConnected = false;
    
    // Attempt reconnection after 2 seconds
    setTimeout(() => {
      console.log('Reconnecting...');
      radio = new RadioClient();
      connect();
    }, 2000);
  });

  radio.subscribe((msg: ResponseMessage) => {
    if (isConnected) {
      console.log('Message received:', msg.type);
    }
  });

  await connect();

  // Periodically test connection
  setInterval(() => {
    if (isConnected) {
      radio.queryDeviceTime();
    }
  }, 30000);
}

// ============================================================================
// Export all examples for testing
// ============================================================================

export {
  example_MonitorMessagesWithFiltering,
  example_RequestResponse,
  RadioClientAsync,
  example_BatchQueries,
  example_MessageStatistics,
  example_DeviceConfiguration,
  example_MessageRouting,
  ContactResolver,
  example_AutoReconnect,
};
