/**
 * MeshCore Radio Client Library
 * Main export file for the hardware serial operations library
 */

// Export all types
export * from './types';

// Export frame protocol utilities
export {
  encodeFrame,
  FrameParser,
  createSimpleCommand,
  createCommandWithByte,
  createCommandWithUInt32,
  createCommandWithData,
  createCommandWithString,
} from './frameProtocol';

// Export message decoder
export {
  decodeFrame,
  getErrorCodeMessage,
  isError,
  isMessage,
} from './messageDecoder';

// Export radio client
export { RadioClient, default } from './radioClient';
