/**
 * Message Decoder
 * Decodes binary frames into typed message objects
 */

import {
  ResponseCode,
  PushCode,
  ErrorCode,
  TextType,
  Frame,
  ResponseMessage,
  DeviceInfo,
  Contact,
  ContactMessage,
  ChannelMessage,
  ChannelInfo,
  AdvertPath,
  SignatureInfo,
  BatteryAndStorage,
  DeviceTime,
  PUB_KEY_SIZE,
  MAX_PATH_SIZE,
  SIGNATURE_SIZE,
} from './types';

/**
 * Decode a binary frame into a ResponseMessage
 * @param frame The frame to decode
 * @returns Decoded ResponseMessage, or unknown type if unrecognized
 */
export function decodeFrame(frame: Frame): ResponseMessage {
  const code = frame.code;
  const payload = frame.payload;

  switch (code) {
    // Response codes (0-23)
    case ResponseCode.RESP_CODE_OK:
      return { type: 'ok', rawPayload: payload };

    case ResponseCode.RESP_CODE_ERR: {
      const errorCode = payload.length > 0 ? payload[0] : 0;
      const errorMessage = getErrorCodeMessage(errorCode);
      return { type: 'error', errorCode, errorMessage, rawPayload: payload };
    }

    case ResponseCode.RESP_CODE_DISABLED:
      return { type: 'disabled', rawPayload: payload };

    case ResponseCode.RESP_CODE_DEVICE_INFO:
      return { type: 'deviceInfo', data: decodeDeviceInfo(payload), rawPayload: payload };

    case ResponseCode.RESP_CODE_CURR_TIME:
      return { type: 'deviceTime', data: decodeDeviceTime(payload), rawPayload: payload };

    case ResponseCode.RESP_CODE_BATT_AND_STORAGE:
      return {
        type: 'batteryAndStorage',
        data: decodeBatteryAndStorage(payload),
        rawPayload: payload,
      };

    case ResponseCode.RESP_CODE_CONTACT:
      try {
        return { type: 'contact', data: decodeContact(payload), rawPayload: payload };
      } catch (e) {
        return { type: 'unknown', code, payload, rawPayload: payload };
      }

    case ResponseCode.RESP_CODE_CONTACTS_START: {
      const totalCount = payload.readUInt32LE(0);
      return { type: 'contactsStart', totalCount, rawPayload: payload };
    }

    case ResponseCode.RESP_CODE_END_OF_CONTACTS:
      return { type: 'endOfContacts', rawPayload: payload };

    case ResponseCode.RESP_CODE_NO_MORE_MESSAGES:
      return { type: 'noMoreMessages', rawPayload: payload };

    case ResponseCode.RESP_CODE_CONTACT_MSG_RECV:
    case ResponseCode.RESP_CODE_CONTACT_MSG_RECV_V3:
      return { type: 'contactMessage', data: decodeContactMessage(payload, code), rawPayload: payload };

    case ResponseCode.RESP_CODE_CHANNEL_MSG_RECV:
    case ResponseCode.RESP_CODE_CHANNEL_MSG_RECV_V3:
      return { type: 'channelMessage', data: decodeChannelMessage(payload, code), rawPayload: payload };

    case ResponseCode.RESP_CODE_CHANNEL_INFO:
      return { type: 'channelInfo', data: decodeChannelInfo(payload), rawPayload: payload };

    case ResponseCode.RESP_CODE_ADVERT_PATH:
      return { type: 'advertPath', data: decodeAdvertPath(payload), rawPayload: payload };

    case ResponseCode.RESP_CODE_SIGNATURE:
      return { type: 'signature', data: decodeSignature(payload), rawPayload: payload };

    case ResponseCode.RESP_CODE_SENT: {
      const sent = decodeSent(payload);
      return { ...sent, rawPayload: payload };
    }

    case ResponseCode.RESP_CODE_CUSTOM_VARS: {
      const vars = decodeCustomVars(payload);
      return { type: 'customVars', vars, rawPayload: payload };
    }

    case ResponseCode.RESP_CODE_PRIVATE_KEY: {
      const key = payload.subarray(0, 64);
      return { type: 'privateKey', key, rawPayload: payload };
    }

    case ResponseCode.RESP_CODE_SIGN_START: {
      const maxDataLen = payload.readUInt32LE(1);
      return { type: 'signStart', maxDataLen, rawPayload: payload };
    }

    case ResponseCode.RESP_CODE_SELF_INFO:
      return { type: 'ok', rawPayload: payload }; // Placeholder

    case ResponseCode.RESP_CODE_EXPORT_CONTACT:
      try {
        return { type: 'contact', data: decodeContact(payload), rawPayload: payload };
      } catch (e) {
        return { type: 'unknown', code, payload, rawPayload: payload };
      }

    // Push codes (0x80-0x8D)
    case PushCode.PUSH_CODE_ADVERT: {
      // Simple advert: just a public key (32 bytes)
      if (payload.length === PUB_KEY_SIZE) {
        return { type: 'simpleAdvert', pubKey: payload, rawPayload: payload };
      }
      // Otherwise try to parse as full contact
      try {
        return { type: 'contact', data: decodeContact(payload), rawPayload: payload };
      } catch (e) {
        return { type: 'unknown', code, payload, rawPayload: payload };
      }
    }

    case PushCode.PUSH_CODE_PATH_UPDATED: {
      const pubKey = payload.subarray(0, PUB_KEY_SIZE);
      return { type: 'pathUpdated', pubKey, rawPayload: payload };
    }

    case PushCode.PUSH_CODE_SEND_CONFIRMED: {
      const ack = payload.subarray(0, 4);
      const tripTimeMs = payload.readUInt32LE(4);
      return { type: 'sendConfirmed', ack, tripTimeMs, rawPayload: payload };
    }

    case PushCode.PUSH_CODE_MSG_WAITING:
      return { type: 'messageWaiting', rawPayload: payload };

    case PushCode.PUSH_CODE_RAW_DATA:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_LOGIN_SUCCESS:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_LOGIN_FAIL:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_STATUS_RESPONSE:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_LOG_RX_DATA: {
      // Format: [0x88][SNR*4 as int8][RSSI as int8][raw packet data...]
      if (payload.length >= 3) {
        const snrRaw = payload.readInt8(0);
        const rssi = payload.readInt8(1);
        const snr = snrRaw / 4;
        const rawPacket = payload.subarray(2);
        return { type: 'logRxData', snr, rssi, rawPacket, rawPayload: payload };
      }
      return { type: 'unknown', code, payload, rawPayload: payload };
    }

    case PushCode.PUSH_CODE_TRACE_DATA:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_NEW_ADVERT:
      try {
        return { type: 'contact', data: decodeContact(payload), rawPayload: payload };
      } catch (e) {
        return { type: 'unknown', code, payload, rawPayload: payload };
      }

    case PushCode.PUSH_CODE_TELEMETRY_RESPONSE:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_BINARY_RESPONSE:
      return { type: 'unknown', code, payload, rawPayload: payload };

    case PushCode.PUSH_CODE_PATH_DISCOVERY_RESPONSE:
      return { type: 'unknown', code, payload, rawPayload: payload };

    // Unknown code
    default:
      return { type: 'unknown', code, payload, rawPayload: payload };
  }
}

// ============================================================================
// DECODERS FOR SPECIFIC MESSAGE TYPES
// ============================================================================

function decodeDeviceInfo(payload: Buffer): DeviceInfo {
  const firmwareCode = payload[1];
  const maxContacts = payload[2];
  const maxGroupChannels = payload[3];
  const blePin = payload.readUInt32LE(4);

  // Build date (12 bytes at offset 8)
  const buildDate = payload
    .subarray(8, 20)
    .toString('utf-8')
    .replace(/\0/g, '')
    .trim();

  // Manufacturer name (40 bytes at offset 20)
  const manufacturerName = payload
    .subarray(20, 60)
    .toString('utf-8')
    .replace(/\0/g, '')
    .trim();

  // Firmware version (20 bytes at offset 60, if present)
  let firmwareVersion = 'Unknown';
  if (payload.length >= 80) {
    firmwareVersion = payload
      .subarray(60, 80)
      .toString('utf-8')
      .replace(/\0/g, '')
      .trim();
  }

  return {
    firmwareVersion,
    firmwareCode,
    buildDate,
    manufacturerName,
    maxContacts,
    maxGroupChannels,
    blePin,
  };
}

function decodeContact(payload: Buffer): Contact {
  let i = 0;

  const pubKey = payload.subarray(i, i + PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;

  const type = payload[i++];
  const flags = payload[i++];
  const outPathLen = payload[i++];
  const outPath = payload.subarray(i, i + MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;

  const name = payload
    .subarray(i, i + 32)
    .toString('utf-8')
    .replace(/\0/g, '')
    .trim();
  i += 32;

  const lastAdvertTimestamp = payload.readUInt32LE(i);
  i += 4;

  const contact: Contact = {
    id: { pubKey },
    name,
    type,
    flags,
    outPathLen,
    outPath,
    lastAdvertTimestamp,
    lastModified: 0,
  };

  // Optional GPS fields (if present)
  if (payload.length >= i + 8) {
    contact.gpsLat = payload.readInt32LE(i);
    i += 4;
    contact.gpsLon = payload.readInt32LE(i);
    i += 4;
  }

  // Optional lastmod field (if present)
  if (payload.length >= i + 4) {
    contact.lastModified = payload.readUInt32LE(i);
  }

  return contact;
}

function decodeContactMessage(
  payload: Buffer,
  code: number
): ContactMessage {
  let i = 0;

  let snrDb: number | undefined;
  if (code === ResponseCode.RESP_CODE_CONTACT_MSG_RECV_V3) {
    snrDb = (payload[i++] as number) / 4; // stored as (snr * 4)
    i += 2; // skip reserved fields
  }

  const fromKeyPrefix = payload.subarray(i, i + 6);
  i += 6;

  const pathLen = payload[i++];
  const textType = payload[i++];

  const timestamp = payload.readUInt32LE(i);
  i += 4;

  // Rest is text
  const text = payload.subarray(i).toString('utf-8');

  return {
    type: 'contact',
    fromKeyPrefix,
    pathLen,
    textType,
    timestamp,
    text,
    snrDb,
  };
}

function decodeChannelMessage(
  payload: Buffer,
  code: number
): ChannelMessage {
  let i = 0;

  let snrDb: number | undefined;
  if (code === ResponseCode.RESP_CODE_CHANNEL_MSG_RECV_V3) {
    snrDb = (payload[i++] as number) / 4; // stored as (snr * 4)
    i += 2; // skip reserved fields
  }

  const channelIndex = payload[i++];
  const pathLen = payload[i++];
  const textType = payload[i++];

  const timestamp = payload.readUInt32LE(i);
  i += 4;

  // Rest is text
  const text = payload.subarray(i).toString('utf-8');

  return {
    type: 'channel',
    channelIndex,
    pathLen,
    textType,
    timestamp,
    text,
    snrDb,
  };
}

function decodeChannelInfo(payload: Buffer): ChannelInfo {
  let i = 0;
  const index = payload[i++];
  const name = payload
    .subarray(i, i + 32)
    .toString('utf-8')
    .replace(/\0/g, '')
    .trim();
  i += 32;
  const secret = payload.subarray(i, i + 16);

  return { index, name, secret };
}

function decodeAdvertPath(payload: Buffer): AdvertPath {
  let i = 0;
  const timestamp = payload.readUInt32LE(i);
  i += 4;
  const pathLen = payload[i++];
  const path = payload.subarray(i, i + pathLen);

  return { timestamp, pathLen, path };
}

/**
 * Parse advert data embedded in a contact or raw advert payload
 * Advert data format (from firmware AdvertDataBuilder::encodeTo):
 * [flags/type][optional lat/lon (8 bytes)][optional extra1 (2 bytes)][optional extra2 (2 bytes)][optional name...]
 * Flags: bit 0-3 = type, bit 4 = ADV_LATLON_MASK, bit 5 = ADV_FEAT1_MASK, bit 6 = ADV_FEAT2_MASK, bit 7 = ADV_NAME_MASK
 */
export interface ParsedAdvertData {
  type: number;
  name?: string;
  latitude?: number; // in degrees
  longitude?: number; // in degrees
  hasLocation: boolean;
}

function parseAdvertData(advertData: Buffer): ParsedAdvertData | null {
  if (advertData.length < 1) return null;

  const flags = advertData[0];
  const type = flags & 0x0F;
  let offset = 1;
  let latitude: number | undefined;
  let longitude: number | undefined;
  let hasLocation = false;

  // ADV_LATLON_MASK = 0x10
  if (flags & 0x10) {
    if (offset + 8 > advertData.length) return null;
    const latInt = advertData.readInt32LE(offset);
    offset += 4;
    const lonInt = advertData.readInt32LE(offset);
    offset += 4;
    latitude = latInt / 1e6;
    longitude = lonInt / 1e6;
    hasLocation = true;
  }

  // ADV_FEAT1_MASK = 0x20
  if (flags & 0x20) {
    offset += 2;
  }

  // ADV_FEAT2_MASK = 0x40
  if (flags & 0x40) {
    offset += 2;
  }

  let name: string | undefined;
  // ADV_NAME_MASK = 0x80
  if (flags & 0x80 && offset < advertData.length) {
    name = advertData
      .subarray(offset)
      .toString('utf-8')
      .replace(/\0/g, '')
      .trim();
  }

  return { type, name, latitude, longitude, hasLocation };
}

function decodeSignature(payload: Buffer): SignatureInfo {
  const signature = payload.subarray(0, SIGNATURE_SIZE);
  return { signature };
}

function decodeBatteryAndStorage(payload: Buffer): BatteryAndStorage {
  let i = 0;
  const batteryMillivolts = payload.readUInt16LE(i);
  i += 2;
  const storageUsedKb = payload.readUInt32LE(i);
  i += 4;
  const storageTotalKb = payload.readUInt32LE(i);

  return { batteryMillivolts, storageUsedKb, storageTotalKb };
}

function decodeDeviceTime(payload: Buffer): DeviceTime {
  const timestamp = payload.readUInt32LE(0);
  return { timestamp };
}

function decodeCustomVars(payload: Buffer): Record<string, string> {
  const text = payload.toString('utf-8');
  const vars: Record<string, string> = {};

  const pairs = text.split(',');
  for (const pair of pairs) {
    const [key, value] = pair.split(':');
    if (key && value) {
      vars[key.trim()] = value.trim();
    }
  }

  return vars;
}

function decodeSent(
  payload: Buffer
): ReturnType<typeof decodeFrame> & { type: 'sent' } {
  let i = 0;
  let flood = false;
  let tag: number | undefined;
  let estimatedTimeoutMs: number | undefined;

  // First byte may indicate if it's a flood send (v3+)
  if (payload.length > 0) {
    flood = payload[i++] !== 0;
  }

  // Next 4 bytes are tag
  if (payload.length >= i + 4) {
    tag = payload.readUInt32LE(i);
    i += 4;
  }

  // Next 4 bytes are estimated timeout
  if (payload.length >= i + 4) {
    estimatedTimeoutMs = payload.readUInt32LE(i);
  }

  return { type: 'sent', flood, tag, estimatedTimeoutMs };
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

export function getErrorCodeMessage(errorCode: number): string {
  switch (errorCode) {
    case ErrorCode.ERR_CODE_UNSUPPORTED_CMD:
      return 'Unsupported command';
    case ErrorCode.ERR_CODE_NOT_FOUND:
      return 'Not found';
    case ErrorCode.ERR_CODE_TABLE_FULL:
      return 'Table full';
    case ErrorCode.ERR_CODE_BAD_STATE:
      return 'Bad state';
    case ErrorCode.ERR_CODE_FILE_IO_ERROR:
      return 'File I/O error';
    case ErrorCode.ERR_CODE_ILLEGAL_ARG:
      return 'Illegal argument';
    default:
      return `Unknown error (${errorCode})`;
  }
}

/**
 * Check if a response is an error
 */
export function isError(message: ResponseMessage): message is Extract<ResponseMessage, { type: 'error' }> {
  return message.type === 'error';
}

/**
 * Check if a response is a message (contact or channel)
 */
export function isMessage(message: ResponseMessage): message is Extract<ResponseMessage, { type: 'contactMessage' | 'channelMessage' }> {
  return message.type === 'contactMessage' || message.type === 'channelMessage';
}
