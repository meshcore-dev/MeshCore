/**
 * Radio Client
 * High-level client for communicating with the MeshCore companion radio firmware
 * Provides command sending and message subscription functionality
 */

import { SerialPort } from 'serialport';
import { EventEmitter } from 'events';
import {
  DEFAULT_BAUD_RATE,
  CommandCode,
  ResponseMessage,
  MessageHandler,
  ErrorHandler,
  Contact,
  ChannelInfo,
  DeviceInfo,
  PUB_KEY_SIZE,
} from './types';
import { FrameParser, encodeFrame, createSimpleCommand, createCommandWithByte, createCommandWithUInt32, createCommandWithData, createCommandWithString } from './frameProtocol';
import { decodeFrame } from './messageDecoder';

/**
 * High-level radio client for MeshCore companion radio
 */
export class RadioClient extends EventEmitter {
  private port: SerialPort | null = null;
  private frameParser = new FrameParser();
  private messageHandlers: MessageHandler[] = [];
  private errorHandlers: ErrorHandler[] = [];

  /**
   * Find available radio ports on the system
   * @returns Array of available serial ports
   */
  public static async findAvailablePorts(): Promise<
    Array<{ path: string; manufacturer?: string }>
  > {
    const ports = await SerialPort.list();
    return ports.map(port => ({
      path: port.path,
      manufacturer: port.manufacturer,
    }));
  }

  /**
   * Automatically detect and connect to a radio
   * @returns Path of the connected radio
   */
  public static async autoDetectAndConnect(): Promise<RadioClient> {
    const ports = await SerialPort.list();
    console.log('Available serial ports:');
    ports.forEach((port, index) => {
      console.log(`  [${index}] ${port.path} - ${port.manufacturer || 'Unknown'}`);
    });

    if (ports.length === 0) {
      throw new Error('No serial ports found');
    }

    // Try to find a likely radio port (ESP32 usually shows as USB or CH340)
    const likelyPort = ports.find(
      port =>
        port.path.includes('ttyUSB') ||
        port.path.includes('COM') ||
        port.manufacturer?.includes('Silicon') ||
        port.manufacturer?.includes('CH340')
    );

    const portPath = likelyPort ? likelyPort.path : ports[0].path;
    console.log(`\nUsing port: ${portPath}`);

    const client = new RadioClient();
    await client.connect(portPath);
    return client;
  }

  /**
   * Connect to the radio on the specified port
   * @param portPath Path to the serial port
   * @param baudRate Baud rate (default 115200)
   */
  public async connect(
    portPath: string,
    baudRate: number = DEFAULT_BAUD_RATE
  ): Promise<void> {
    return new Promise((resolve, reject) => {
      this.port = new SerialPort(
        { path: portPath, baudRate, autoOpen: false },
        err => {
          if (err) {
            reject(err);
          }
        }
      );

      this.port.on('data', (data: Buffer) => this.handleSerialData(data));
      this.port.on('error', (err: Error) => this.handleSerialError(err));

      this.port.open((err: Error | null) => {
        if (err) {
          reject(err);
        } else {
          console.log(`Connected to ${portPath} at ${baudRate} baud`);
          resolve();
        }
      });
    });
  }

  /**
   * Disconnect from the radio
   */
  public disconnect(): void {
    if (this.port && this.port.isOpen) {
      this.port.close();
      console.log('Disconnected');
    }
  }

  /**
   * Subscribe to messages from the radio
   * @param handler Function to call for each message
   * @returns Unsubscribe function
   */
  public subscribe(handler: MessageHandler): () => void {
    this.messageHandlers.push(handler);

    // Return unsubscribe function
    return () => {
      const index = this.messageHandlers.indexOf(handler);
      if (index > -1) {
        this.messageHandlers.splice(index, 1);
      }
    };
  }

  /**
   * Subscribe to error events
   * @param handler Function to call on error
   * @returns Unsubscribe function
   */
  public onError(handler: ErrorHandler): () => void {
    this.errorHandlers.push(handler);

    // Return unsubscribe function
    return () => {
      const index = this.errorHandlers.indexOf(handler);
      if (index > -1) {
        this.errorHandlers.splice(index, 1);
      }
    };
  }

  // ============================================================================
  // COMMAND SENDING METHODS
  // ============================================================================

  /**
   * Query device information
   */
  public queryDeviceInfo(protocolVersion: number = 7): void {
    const payload = Buffer.alloc(2);
    payload[0] = CommandCode.CMD_DEVICE_QUERY;
    payload[1] = protocolVersion;
    this.sendCommand(payload);
  }

  /**
   * Request current device time
   */
  public queryDeviceTime(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_GET_DEVICE_TIME));
  }

  /**
   * Set device time
   * @param unixTimestamp Unix timestamp in seconds
   */
  public setDeviceTime(unixTimestamp: number): void {
    this.sendCommand(
      createCommandWithUInt32(
        CommandCode.CMD_SET_DEVICE_TIME,
        unixTimestamp
      )
    );
  }

  /**
   * Get battery and storage information
   */
  public queryBatteryAndStorage(): void {
    this.sendCommand(
      createSimpleCommand(CommandCode.CMD_GET_BATT_AND_STORAGE)
    );
  }

  /**
   * Request contacts list
   * @param since Optional timestamp to get only contacts modified since this time
   */
  public getContacts(since?: number): void {
    if (since !== undefined) {
      this.sendCommand(
        createCommandWithUInt32(CommandCode.CMD_GET_CONTACTS, since)
      );
    } else {
      this.sendCommand(createSimpleCommand(CommandCode.CMD_GET_CONTACTS));
    }
  }

  /**
   * Sync next message from offline queue
   */
  public syncNextMessage(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_SYNC_NEXT_MESSAGE));
  }

  /**
   * Send a text message to a contact
   * @param pubKey 32-byte public key of recipient
   * @param text Message text
   */
  public sendTextMessage(pubKey: Buffer, text: string): void {
    if (pubKey.length !== PUB_KEY_SIZE) {
      throw new Error(
        `Invalid public key size: ${pubKey.length}, expected ${PUB_KEY_SIZE}`
      );
    }

    const payload = Buffer.alloc(1 + PUB_KEY_SIZE + text.length);
    let i = 0;
    payload[i++] = CommandCode.CMD_SEND_TXT_MSG;
    pubKey.copy(payload, i);
    i += PUB_KEY_SIZE;
    i += payload.write(text, i, 'utf-8');

    this.sendCommand(payload.subarray(0, i));
  }

  /**
   * Send a text message to a channel
   * @param channelIndex Channel index
   * @param text Message text
   */
  public sendChannelMessage(channelIndex: number, text: string): void {
    const payload = Buffer.alloc(1 + 1 + text.length);
    let i = 0;
    payload[i++] = CommandCode.CMD_SEND_CHANNEL_TXT_MSG;
    payload[i++] = channelIndex & 0xff;
    i += payload.write(text, i, 'utf-8');

    this.sendCommand(payload.subarray(0, i));
  }

  /**
   * Set advertised name
   * @param name New device name
   */
  public setAdvertName(name: string): void {
    this.sendCommand(
      createCommandWithString(CommandCode.CMD_SET_ADVERT_NAME, name)
    );
  }

  /**
   * Set GPS location
   * @param latitude Latitude in degrees (multiplied by 1e6 for storage)
   * @param longitude Longitude in degrees (multiplied by 1e6 for storage)
   * @param altitude Optional altitude (not yet supported)
   */
  public setAdvertLocation(
    latitude: number,
    longitude: number,
    altitude?: number
  ): void {
    const latInt = Math.round(latitude * 1e6);
    const lonInt = Math.round(longitude * 1e6);

    const payload = Buffer.alloc(altitude !== undefined ? 13 : 9);
    let i = 0;
    payload[i++] = CommandCode.CMD_SET_ADVERT_LATLON;
    payload.writeInt32LE(latInt, i);
    i += 4;
    payload.writeInt32LE(lonInt, i);
    i += 4;

    if (altitude !== undefined) {
      const altInt = Math.round(altitude);
      payload.writeInt32LE(altInt, i);
    }

    this.sendCommand(payload);
  }

  /**
   * Get a specific channel
   * @param channelIndex Channel index
   */
  public getChannel(channelIndex: number): void {
    this.sendCommand(
      createCommandWithByte(CommandCode.CMD_GET_CHANNEL, channelIndex)
    );
  }

  /**
   * Set a channel
   * @param channelIndex Channel index
   * @param name Channel name (max 32 bytes)
   * @param secret Channel secret (16 bytes for 128-bit)
   */
  public setChannel(
    channelIndex: number,
    name: string,
    secret: Buffer
  ): void {
    if (secret.length < 16) {
      throw new Error('Channel secret must be at least 16 bytes');
    }

    const nameBuffer = Buffer.alloc(32);
    nameBuffer.write(name);

    const payload = Buffer.alloc(1 + 1 + 32 + 16);
    let i = 0;
    payload[i++] = CommandCode.CMD_SET_CHANNEL;
    payload[i++] = channelIndex;
    nameBuffer.copy(payload, i);
    i += 32;
    secret.subarray(0, 16).copy(payload, i);

    this.sendCommand(payload);
  }

  /**
   * Export private key (if enabled)
   */
  public exportPrivateKey(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_EXPORT_PRIVATE_KEY));
  }

  /**
   * Get custom variables
   */
  public getCustomVars(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_GET_CUSTOM_VARS));
  }

  /**
   * Set a custom variable
   * @param name Variable name
   * @param value Variable value
   */
  public setCustomVar(name: string, value: string): void {
    const varStr = `${name}:${value}`;
    this.sendCommand(
      createCommandWithString(CommandCode.CMD_SET_CUSTOM_VAR, varStr)
    );
  }

  /**
   * Reboot the device
   */
  public reboot(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_REBOOT));
  }

  /**
   * Factory reset (requires "reset" confirmation)
   */
  public factoryReset(): void {
    const payload = Buffer.alloc(1 + 5);
    payload[0] = CommandCode.CMD_FACTORY_RESET;
    payload.write('reset', 1);
    this.sendCommand(payload);
  }

  /**
   * Start signing data
   */
  public signStart(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_SIGN_START));
  }

  /**
   * Send data to be signed (must call signStart first)
   * @param data Data to sign
   */
  public signData(data: Buffer): void {
    this.sendCommand(
      createCommandWithData(CommandCode.CMD_SIGN_DATA, data)
    );
  }

  /**
   * Finish signing and get signature
   */
  public signFinish(): void {
    this.sendCommand(createSimpleCommand(CommandCode.CMD_SIGN_FINISH));
  }

  /**
   * Generic command sender for custom commands
   * @param payload Complete command payload
   */
  public sendCommand(payload: Buffer): void {
    if (!this.port || !this.port.isOpen) {
      this.emitError(new Error('Serial port is not open'));
      return;
    }

    try {
      const frame = encodeFrame(payload);
      this.port.write(frame, (err?: Error | null) => {
        if (err) {
          this.emitError(err);
        }
      });
    } catch (error) {
      this.emitError(
        error instanceof Error ? error : new Error(String(error))
      );
    }
  }

  // ============================================================================
  // PRIVATE METHODS
  // ============================================================================

  private handleSerialData(data: Buffer): void {
    const frames = this.frameParser.addData(data);
    for (const frame of frames) {
      try {
        const message = decodeFrame(frame);
        this.emitMessage(message);
      } catch (error) {
        this.emitError(
          error instanceof Error ? error : new Error(String(error))
        );
      }
    }
  }

  private handleSerialError(error: Error): void {
    this.emitError(error);
  }

  private emitMessage(message: ResponseMessage): void {
    // Emit event for debugging
    this.emit('message', message);

    // Call all subscribers
    for (const handler of this.messageHandlers) {
      try {
        handler(message);
      } catch (error) {
        console.error('Error in message handler:', error);
      }
    }
  }

  private emitError(error: Error): void {
    // Emit event for debugging
    this.emit('error', error);

    // Call all error handlers
    for (const handler of this.errorHandlers) {
      try {
        handler(error);
      } catch (error) {
        console.error('Error in error handler:', error);
      }
    }
  }

  /**
   * Check if connected
   */
  public isConnected(): boolean {
    return this.port?.isOpen ?? false;
  }
}

// ============================================================================
// CONVENIENCE EXPORT
// ============================================================================

export default RadioClient;
