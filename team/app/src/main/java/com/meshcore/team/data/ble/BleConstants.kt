package com.meshcore.team.data.ble

/**
 * BLE Protocol Constants
 * Based on MeshCore companion radio BLE implementation
 * Reference: src/helpers/esp32/SerialBLEInterface.cpp
 */
object BleConstants {
    // Service & Characteristics (Nordic UART Service compatible)
    const val SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
    const val RX_CHARACTERISTIC_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write
    const val TX_CHARACTERISTIC_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Read + Notify
    
    // Frame protocol
    const val MAX_FRAME_SIZE = 172  // bytes
    const val MIN_WRITE_INTERVAL_MS = 60L  // milliseconds between writes
    
    // Command codes
    object Commands {
        const val CMD_APP_START: Byte = 1
        const val CMD_SEND_TXT_MSG: Byte = 2
        const val CMD_SEND_CHANNEL_TXT_MSG: Byte = 3
        const val CMD_GET_CONTACTS: Byte = 4
        const val CMD_GET_DEVICE_TIME: Byte = 5
        const val CMD_SET_DEVICE_TIME: Byte = 6
        const val CMD_SEND_SELF_ADVERT: Byte = 7
        const val CMD_SET_ADVERT_NAME: Byte = 8
        const val CMD_SYNC_NEXT_MESSAGE: Byte = 10
        const val CMD_SET_ADVERT_LATLON: Byte = 14
        const val CMD_GET_CHANNEL: Byte = 31
        const val CMD_SET_CHANNEL: Byte = 32
    }
    
    // Response codes
    object Responses {
        const val RESP_CODE_OK: Byte = 0
        const val RESP_CODE_ERR: Byte = 1
        const val RESP_CODE_CONTACTS_START: Byte = 2
        const val RESP_CODE_CONTACT: Byte = 3
        const val RESP_CODE_END_OF_CONTACTS: Byte = 4
        const val RESP_CODE_SELF_INFO: Byte = 5
        const val RESP_CODE_SENT: Byte = 6
        const val RESP_CODE_NO_MORE_MESSAGES: Byte = 10
        const val RESP_CODE_CONTACT_MSG_RECV_V3: Byte = 16
        const val RESP_CODE_CHANNEL_MSG_RECV_V3: Byte = 17
        const val RESP_CODE_CHANNEL_INFO: Byte = 18
    }
    
    // Push notification codes (async from device)
    object PushCodes {
        const val PUSH_CODE_ADVERT: Byte = 0x80.toByte()
        const val PUSH_CODE_PATH_UPDATED: Byte = 0x81.toByte()
        const val PUSH_CODE_SEND_CONFIRMED: Byte = 0x82.toByte()
        const val PUSH_CODE_MSG_WAITING: Byte = 0x83.toByte()
        const val PUSH_CODE_NEW_ADVERT: Byte = 0x8A.toByte()
    }
    
    // Error codes
    object Errors {
        const val ERR_CODE_UNSUPPORTED_CMD: Byte = 1
        const val ERR_CODE_NOT_FOUND: Byte = 2
        const val ERR_CODE_TABLE_FULL: Byte = 3
        const val ERR_CODE_BAD_STATE: Byte = 4
        const val ERR_CODE_FILE_IO_ERROR: Byte = 5
        const val ERR_CODE_ILLEGAL_ARG: Byte = 6
    }
    
    // Public channel
    const val PUBLIC_CHANNEL_PSK = "izOH6cXN6mrJ5e26oRXNcg=="
}
