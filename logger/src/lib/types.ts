/**
 * Hardware Serial Protocol Types and Constants
 * This file defines all command codes, response codes, and TypeScript interfaces
 * for the MeshCore companion radio firmware serial protocol.
 */

// ============================================================================
// FRAME PROTOCOL CONSTANTS
// ============================================================================

export const FRAME_HEADER_SEND = '<';
export const FRAME_HEADER_RECV = '>';
export const MAX_FRAME_SIZE = 172;
export const DEFAULT_BAUD_RATE = 115200;

// ============================================================================
// SIZE CONSTANTS
// ============================================================================

export const PUB_KEY_SIZE = 32;
export const SHARED_SECRET_SIZE = 32;
export const MAX_PATH_SIZE = 32;
export const MAX_CONTACTS = 100; // typical value
export const MAX_GROUP_CHANNELS = 10;
export const SIGNATURE_SIZE = 64;
export const MAX_SIGN_DATA_LEN = 8 * 1024; // 8K

// ============================================================================
// COMMAND CODES
// ============================================================================

export enum CommandCode {
  CMD_APP_START = 1,
  CMD_SEND_TXT_MSG = 2,
  CMD_SEND_CHANNEL_TXT_MSG = 3,
  CMD_GET_CONTACTS = 4,
  CMD_GET_DEVICE_TIME = 5,
  CMD_SET_DEVICE_TIME = 6,
  CMD_SEND_SELF_ADVERT = 7,
  CMD_SET_ADVERT_NAME = 8,
  CMD_ADD_UPDATE_CONTACT = 9,
  CMD_SYNC_NEXT_MESSAGE = 10,
  CMD_SET_RADIO_PARAMS = 11,
  CMD_SET_RADIO_TX_POWER = 12,
  CMD_RESET_PATH = 13,
  CMD_SET_ADVERT_LATLON = 14,
  CMD_REMOVE_CONTACT = 15,
  CMD_SHARE_CONTACT = 16,
  CMD_EXPORT_CONTACT = 17,
  CMD_IMPORT_CONTACT = 18,
  CMD_REBOOT = 19,
  CMD_GET_BATT_AND_STORAGE = 20,
  CMD_SET_TUNING_PARAMS = 21,
  CMD_DEVICE_QUERY = 22,
  CMD_EXPORT_PRIVATE_KEY = 23,
  CMD_IMPORT_PRIVATE_KEY = 24,
  CMD_SEND_RAW_DATA = 25,
  CMD_SEND_LOGIN = 26,
  CMD_SEND_STATUS_REQ = 27,
  CMD_HAS_CONNECTION = 28,
  CMD_LOGOUT = 29,
  CMD_GET_CONTACT_BY_KEY = 30,
  CMD_GET_CHANNEL = 31,
  CMD_SET_CHANNEL = 32,
  CMD_SIGN_START = 33,
  CMD_SIGN_DATA = 34,
  CMD_SIGN_FINISH = 35,
  CMD_SEND_TRACE_PATH = 36,
  CMD_SET_DEVICE_PIN = 37,
  CMD_SET_OTHER_PARAMS = 38,
  CMD_SEND_TELEMETRY_REQ = 39,
  CMD_GET_CUSTOM_VARS = 40,
  CMD_SET_CUSTOM_VAR = 41,
  CMD_GET_ADVERT_PATH = 42,
  CMD_GET_TUNING_PARAMS = 43,
  CMD_SEND_BINARY_REQ = 50,
  CMD_FACTORY_RESET = 51,
  CMD_SEND_PATH_DISCOVERY_REQ = 52,
}

// ============================================================================
// RESPONSE CODES
// ============================================================================

export enum ResponseCode {
  RESP_CODE_OK = 0,
  RESP_CODE_ERR = 1,
  RESP_CODE_CONTACTS_START = 2,
  RESP_CODE_CONTACT = 3,
  RESP_CODE_END_OF_CONTACTS = 4,
  RESP_CODE_SELF_INFO = 5,
  RESP_CODE_SENT = 6,
  RESP_CODE_CONTACT_MSG_RECV = 7,
  RESP_CODE_CHANNEL_MSG_RECV = 8,
  RESP_CODE_CURR_TIME = 9,
  RESP_CODE_NO_MORE_MESSAGES = 10,
  RESP_CODE_EXPORT_CONTACT = 11,
  RESP_CODE_BATT_AND_STORAGE = 12,
  RESP_CODE_DEVICE_INFO = 13,
  RESP_CODE_PRIVATE_KEY = 14,
  RESP_CODE_DISABLED = 15,
  RESP_CODE_CONTACT_MSG_RECV_V3 = 16,
  RESP_CODE_CHANNEL_MSG_RECV_V3 = 17,
  RESP_CODE_CHANNEL_INFO = 18,
  RESP_CODE_SIGN_START = 19,
  RESP_CODE_SIGNATURE = 20,
  RESP_CODE_CUSTOM_VARS = 21,
  RESP_CODE_ADVERT_PATH = 22,
  RESP_CODE_TUNING_PARAMS = 23,
}

// ============================================================================
// PUSH CODES (messages pushed to client at any time)
// ============================================================================

export enum PushCode {
  PUSH_CODE_ADVERT = 0x80,
  PUSH_CODE_PATH_UPDATED = 0x81,
  PUSH_CODE_SEND_CONFIRMED = 0x82,
  PUSH_CODE_MSG_WAITING = 0x83,
  PUSH_CODE_RAW_DATA = 0x84,
  PUSH_CODE_LOGIN_SUCCESS = 0x85,
  PUSH_CODE_LOGIN_FAIL = 0x86,
  PUSH_CODE_STATUS_RESPONSE = 0x87,
  PUSH_CODE_LOG_RX_DATA = 0x88,
  PUSH_CODE_TRACE_DATA = 0x89,
  PUSH_CODE_NEW_ADVERT = 0x8a,
  PUSH_CODE_TELEMETRY_RESPONSE = 0x8b,
  PUSH_CODE_BINARY_RESPONSE = 0x8c,
  PUSH_CODE_PATH_DISCOVERY_RESPONSE = 0x8d,
}

// ============================================================================
// ERROR CODES
// ============================================================================

export enum ErrorCode {
  ERR_CODE_UNSUPPORTED_CMD = 1,
  ERR_CODE_NOT_FOUND = 2,
  ERR_CODE_TABLE_FULL = 3,
  ERR_CODE_BAD_STATE = 4,
  ERR_CODE_FILE_IO_ERROR = 5,
  ERR_CODE_ILLEGAL_ARG = 6,
}

// ============================================================================
// MESSAGE TEXT TYPES
// ============================================================================

export enum TextType {
  TXT_TYPE_PLAIN = 0,
  TXT_TYPE_SIGNED_PLAIN = 1,
  TXT_TYPE_CLI_DATA = 2,
}

// ============================================================================
// ADVERTISEMENT TYPES
// ============================================================================

export enum AdvertType {
  ADV_TYPE_NONE = 0,
  ADV_TYPE_SELF_ADVERT = 1,
}

// ============================================================================
// DATA STRUCTURES - DEVICE INFO
// ============================================================================

export interface DeviceInfo {
  firmwareVersion: string;
  firmwareCode: number;
  buildDate: string;
  manufacturerName: string;
  maxContacts: number;
  maxGroupChannels: number;
  blePin: number;
}

// ============================================================================
// DATA STRUCTURES - CONTACTS
// ============================================================================

export interface ContactIdentity {
  pubKey: Buffer; // 32 bytes
}

export interface Contact {
  id: ContactIdentity;
  name: string;
  type: number;
  flags: number;
  outPathLen: number;
  outPath: Buffer; // MAX_PATH_SIZE bytes
  lastAdvertTimestamp: number; // unix timestamp
  lastModified: number; // unix timestamp
  gpsLat?: number; // 6 decimal places (can be divided by 1e6)
  gpsLon?: number; // 6 decimal places
}

// ============================================================================
// DATA STRUCTURES - CHANNELS
// ============================================================================

export interface ChannelInfo {
  index: number;
  name: string;
  secret: Buffer; // 16 bytes for now, supports up to 32
}

// ============================================================================
// DATA STRUCTURES - MESSAGES
// ============================================================================

export interface ContactMessage {
  type: 'contact';
  fromKeyPrefix: Buffer; // 6 bytes
  pathLen: number;
  textType: number;
  timestamp: number;
  text: string;
  snrDb?: number; // v3+ only
}

export interface ChannelMessage {
  type: 'channel';
  channelIndex: number;
  pathLen: number;
  textType: number;
  timestamp: number;
  text: string;
  snrDb?: number; // v3+ only
}

export type IncomingMessage = ContactMessage | ChannelMessage;

// ============================================================================
// DATA STRUCTURES - BATTERY AND STORAGE
// ============================================================================

export interface BatteryAndStorage {
  batteryMillivolts: number;
  storageUsedKb: number;
  storageTotalKb: number;
}

// ============================================================================
// DATA STRUCTURES - TIME
// ============================================================================

export interface DeviceTime {
  timestamp: number; // unix timestamp
}

// ============================================================================
// DATA STRUCTURES - ADVERT PATH
// ============================================================================

export interface AdvertPath {
  timestamp: number;
  pathLen: number;
  path: Buffer;
}

// ============================================================================
// DATA STRUCTURES - SIGNATURE
// ============================================================================

export interface SignatureInfo {
  signature: Buffer; // SIGNATURE_SIZE (64) bytes
}

// ============================================================================
// FRAME TYPE DEFINITIONS
// ============================================================================

export interface Frame {
  code: number;
  payload: Buffer;
}

// ============================================================================
// RESPONSE WRAPPER TYPES
// ============================================================================

export type ResponseMessage =
  | { type: 'ok' }
  | { type: 'error'; errorCode: number; errorMessage: string }
  | { type: 'deviceInfo'; data: DeviceInfo }
  | { type: 'deviceTime'; data: DeviceTime }
  | { type: 'batteryAndStorage'; data: BatteryAndStorage }
  | { type: 'contact'; data: Contact }
  | { type: 'contactsStart'; totalCount: number }
  | { type: 'endOfContacts' }
  | { type: 'contactMessage'; data: ContactMessage }
  | { type: 'channelMessage'; data: ChannelMessage }
  | { type: 'channelInfo'; data: ChannelInfo }
  | { type: 'advertPath'; data: AdvertPath }
  | { type: 'signature'; data: SignatureInfo }
  | { type: 'sent'; tag?: number; estimatedTimeoutMs?: number; flood?: boolean }
  | { type: 'noMoreMessages' }
  | { type: 'disabled' }
  | { type: 'customVars'; vars: Record<string, string> }
  | { type: 'privateKey'; key: Buffer }
  | { type: 'pathUpdated'; pubKey: Buffer }
  | { type: 'sendConfirmed'; ack: Buffer; tripTimeMs: number }
  | { type: 'messageWaiting' }
  | { type: 'signStart'; maxDataLen: number }
  | { type: 'unknown'; code: number; payload: Buffer };

// ============================================================================
// MESSAGE SUBSCRIPTION TYPE
// ============================================================================

export type MessageHandler = (message: ResponseMessage) => void;
export type ErrorHandler = (error: Error) => void;
