/**
 * Frame Protocol Encoder/Decoder
 * Handles the low-level frame protocol for the MeshCore companion radio.
 * Frame format: '<' + length_lsb + length_msb + payload
 */

import {
  FRAME_HEADER_SEND,
  FRAME_HEADER_RECV,
  MAX_FRAME_SIZE,
  Frame,
} from './types';

/**
 * Encodes a payload into a complete frame for sending
 * @param payload The payload buffer to encode
 * @returns The complete frame with header and length
 * @throws Error if payload exceeds MAX_FRAME_SIZE
 */
export function encodeFrame(payload: Buffer): Buffer {
  if (payload.length > MAX_FRAME_SIZE) {
    throw new Error(`Payload too large: ${payload.length} > ${MAX_FRAME_SIZE}`);
  }

  const frame = Buffer.alloc(3 + payload.length);
  frame[0] = FRAME_HEADER_SEND.charCodeAt(0);
  frame[1] = payload.length & 0xff;
  frame[2] = (payload.length >> 8) & 0xff;
  payload.copy(frame, 3);

  return frame;
}

/**
 * Frame parser state machine
 */
export class FrameParser {
  private buffer: Buffer = Buffer.alloc(0);

  /**
   * Add data to the internal buffer and attempt to parse frames
   * @param data New data to add
   * @returns Array of parsed frames, empty array if no complete frames available
   */
  public addData(data: Buffer): Frame[] {
    this.buffer = Buffer.concat([this.buffer, data]);
    const frames: Frame[] = [];

    while (true) {
      const frame = this.tryParseFrame();
      if (!frame) {
        break;
      }
      frames.push(frame);
    }

    return frames;
  }

  /**
   * Attempt to parse a single frame from the buffer
   * @returns Parsed frame or null if no complete frame available
   */
  private tryParseFrame(): Frame | null {
    // Look for frame header
    const headerIndex = this.buffer.indexOf(FRAME_HEADER_RECV.charCodeAt(0));

    if (headerIndex === -1) {
      // No frame header found, clear buffer
      this.buffer = Buffer.alloc(0);
      return null;
    }

    if (headerIndex > 0) {
      // Skip data before the frame header
      this.buffer = this.buffer.subarray(headerIndex);
    }

    // Check if we have enough data for the header + length bytes
    if (this.buffer.length < 3) {
      return null;
    }

    // Parse frame length (little-endian)
    const lenLsb = this.buffer[1];
    const lenMsb = this.buffer[2];
    const frameLen = lenLsb | (lenMsb << 8);

    // Check if frame length is valid
    if (frameLen > MAX_FRAME_SIZE) {
      console.warn(
        `Invalid frame length: ${frameLen}, skipping byte and trying again`
      );
      this.buffer = this.buffer.subarray(1);
      return this.tryParseFrame();
    }

    // Check if we have the complete frame
    const totalLen = 3 + frameLen;
    if (this.buffer.length < totalLen) {
      return null; // Wait for more data
    }

    // Extract frame payload and remove from buffer
    const payload = this.buffer.subarray(3, totalLen);
    this.buffer = this.buffer.subarray(totalLen);

    return {
      code: payload[0],
      payload: payload.length > 1 ? payload.subarray(1) : Buffer.alloc(0),
    };
  }

  /**
   * Clear the internal buffer (useful for error recovery)
   */
  public clear(): void {
    this.buffer = Buffer.alloc(0);
  }

  /**
   * Get the current buffer size (for debugging)
   */
  public getBufferSize(): number {
    return this.buffer.length;
  }
}

/**
 * Helper function to create a simple command payload
 * @param commandCode The command code
 * @returns Buffer containing just the command code
 */
export function createSimpleCommand(commandCode: number): Buffer {
  const payload = Buffer.alloc(1);
  payload[0] = commandCode;
  return payload;
}

/**
 * Helper function to create a command with a single byte argument
 * @param commandCode The command code
 * @param arg The argument byte
 * @returns Buffer containing the command and argument
 */
export function createCommandWithByte(commandCode: number, arg: number): Buffer {
  const payload = Buffer.alloc(2);
  payload[0] = commandCode;
  payload[1] = arg & 0xff;
  return payload;
}

/**
 * Helper function to create a command with a 32-bit argument
 * @param commandCode The command code
 * @param arg The argument (32-bit unsigned)
 * @returns Buffer containing the command and argument (little-endian)
 */
export function createCommandWithUInt32(
  commandCode: number,
  arg: number
): Buffer {
  const payload = Buffer.alloc(5);
  payload[0] = commandCode;
  payload.writeUInt32LE(arg, 1);
  return payload;
}

/**
 * Helper function to create a command with binary data
 * @param commandCode The command code
 * @param data The binary data to append
 * @returns Buffer containing the command and data
 */
export function createCommandWithData(
  commandCode: number,
  data: Buffer
): Buffer {
  const payload = Buffer.alloc(1 + data.length);
  payload[0] = commandCode;
  data.copy(payload, 1);
  return payload;
}

/**
 * Helper function to create a command with a string
 * @param commandCode The command code
 * @param str The string to append
 * @param maxLen Maximum length of the string (default 171, to fit in max frame)
 * @returns Buffer containing the command and string
 */
export function createCommandWithString(
  commandCode: number,
  str: string,
  maxLen: number = MAX_FRAME_SIZE - 1
): Buffer {
  const strBytes = Buffer.from(str, 'utf-8');
  const len = Math.min(strBytes.length, maxLen);
  const payload = Buffer.alloc(1 + len);
  payload[0] = commandCode;
  strBytes.copy(payload, 1, 0, len);
  return payload;
}
