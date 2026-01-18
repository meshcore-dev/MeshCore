#include "BitchatBLEService.h"

#ifdef NRF52_PLATFORM

#include <Arduino.h>

#if BITCHAT_DEBUG
  #define BITCHAT_DEBUG_PRINTLN(F, ...) Serial.printf("BITCHAT: " F "\n", ##__VA_ARGS__)
#else
  #define BITCHAT_DEBUG_PRINTLN(...) {}
#endif

// Bitchat service UUID: F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C
// Bluefruit uses little-endian byte order for UUIDs
static const uint8_t BITCHAT_SERVICE_UUID_BYTES[] = {
    0x5C, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B,
    0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4
};

// Bitchat characteristic UUID: F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5D
static const uint8_t BITCHAT_CHARACTERISTIC_UUID_BYTES[] = {
    0x5D, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B,
    0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4
};

// Singleton instance for static callback access
BitchatBLEService* BitchatBLEService::_instance = nullptr;

BitchatBLEService::BitchatBLEService()
    : _service(BITCHAT_SERVICE_UUID_BYTES)
    , _characteristic(BITCHAT_CHARACTERISTIC_UUID_BYTES)
    , _callback(nullptr)
    , _serviceActive(false)
    , _bitchatClientCount(0)
    , _clientSubscribed(false)
    , _pendingConnect(false)
    , _pendingData(false)
    , _writeBufferOffset(0)
    , _lastWriteTime(0)
    , _queueHead(0)
    , _queueTail(0)
{
    memset(_writeBuffer, 0, sizeof(_writeBuffer));
    memset(_deviceName, 0, sizeof(_deviceName));
    strcpy(_deviceName, "Bitchat");
    for (size_t i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        _messageQueue[i].valid = false;
    }
    _instance = this;
}

bool BitchatBLEService::beginStandalone(const char* deviceName, BitchatBLECallback* callback) {
    if (callback == nullptr) {
        return false;
    }

    _callback = callback;
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
    _deviceName[sizeof(_deviceName) - 1] = '\0';

    // Initialize Bluefruit
    Bluefruit.begin();
    Bluefruit.setTxPower(4);  // Max power for better range

    // Set up connection callbacks
    Bluefruit.Periph.setConnectCallback(onConnect);
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);

    // Bitchat uses open security (no PIN required)
    Bluefruit.Security.setMITM(false);
    Bluefruit.Security.setIOCaps(false, false, false);

    // Set device name (filter out non-ASCII characters for BLE)
    char safeName[32];
    size_t j = 0;
    for (size_t i = 0; deviceName[i] != '\0' && j < sizeof(safeName) - 1; i++) {
        if (deviceName[i] >= 0x20 && deviceName[i] <= 0x7E) {
            safeName[j++] = deviceName[i];
        }
    }
    safeName[j] = '\0';
    if (j == 0) strcpy(safeName, "Bitchat");

    Bluefruit.setName(safeName);

    // Configure the Bitchat service
    _service.begin();

    // Configure the characteristic with READ, WRITE, WRITE_NR, NOTIFY properties
    _characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY);
    _characteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);  // Open security
    _characteristic.setMaxLen(512);  // Support large Bitchat messages
    _characteristic.setWriteCallback(onCharacteristicWrite);
    _characteristic.setCccdWriteCallback(onCharacteristicCccdWrite);
    _characteristic.begin();

    _serviceActive = true;
    BITCHAT_DEBUG_PRINTLN("Bitchat BLE service initialized: %s", safeName);

    return true;
}

void BitchatBLEService::startAdvertising() {
    // Clear any previous advertising data
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();

    // Set Bitchat UUID in MAIN advertisement (required for Bitchat app discovery)
    // The Bitchat Android app filters on service UUID in main advertisement packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(_service);

    // Put device name in scan response (not main adv - no room with 128-bit UUID)
    Bluefruit.ScanResponse.addName();

    // Configure advertising parameters
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in units of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);      // seconds in fast mode
    Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising

    BITCHAT_DEBUG_PRINTLN("BLE advertising started");
}

void BitchatBLEService::onServerDisconnect() {
    if (_bitchatClientCount > 0) {
        _bitchatClientCount--;
    }

    if (_bitchatClientCount == 0) {
        _clientSubscribed = false;
        clearWriteBuffer();
        if (_callback != nullptr) {
            _callback->onBitchatClientDisconnect();
        }
    }
}

void BitchatBLEService::clearWriteBuffer() {
    _writeBufferOffset = 0;
    memset(_writeBuffer, 0, sizeof(_writeBuffer));
}

bool BitchatBLEService::queueMessage(const BitchatMessage& msg) {
    size_t nextTail = (_queueTail + 1) % MESSAGE_QUEUE_SIZE;

    if (nextTail == _queueHead) {
        BITCHAT_DEBUG_PRINTLN("Message queue full, dropping message");
        return false;
    }

    _messageQueue[_queueTail].msg = msg;
    _messageQueue[_queueTail].valid = true;
    _queueTail = nextTail;

    return true;
}

void BitchatBLEService::processQueue() {
    while (_queueHead != _queueTail) {
        if (_messageQueue[_queueHead].valid) {
            _messageQueue[_queueHead].valid = false;

            if (_callback != nullptr) {
                _callback->onBitchatMessageReceived(_messageQueue[_queueHead].msg);
            }
        }
        _queueHead = (_queueHead + 1) % MESSAGE_QUEUE_SIZE;
    }
}

void BitchatBLEService::loop() {
    uint32_t now = millis();

    // Handle deferred connect callback
    if (_pendingConnect) {
        _pendingConnect = false;
        if (_callback != nullptr) {
            _callback->onBitchatClientConnect();
        }
    }

    // Handle deferred data processing
    // Wait 100ms after last write before processing to allow multi-chunk messages to arrive
    if (_pendingData && (now - _lastWriteTime >= 100)) {
        _pendingData = false;
        BITCHAT_DEBUG_PRINTLN("Processing %zu buffered bytes", _writeBufferOffset);

        BitchatMessage msg;
        if (BitchatProtocol::parseMessage(_writeBuffer, _writeBufferOffset, msg)) {
            if (BitchatProtocol::validateMessage(msg)) {
                BITCHAT_DEBUG_PRINTLN("Received Bitchat message: type=%02X, len=%d", msg.type, msg.payloadLength);
                queueMessage(msg);
            } else {
                BITCHAT_DEBUG_PRINTLN("Invalid Bitchat message received");
            }
            clearWriteBuffer();
        } else if (_writeBufferOffset >= BITCHAT_HEADER_SIZE) {
            size_t expectedMin = BitchatProtocol::getMessageSize(msg);
            if (_writeBufferOffset > expectedMin + 100) {
                BITCHAT_DEBUG_PRINTLN("Write buffer contains unparseable data, clearing");
                clearWriteBuffer();
            }
        }
    }

    // Check for write buffer timeout
    if (_writeBufferOffset > 0) {
        if (now - _lastWriteTime > WRITE_TIMEOUT_MS) {
            BITCHAT_DEBUG_PRINTLN("Write buffer timeout, clearing");
            clearWriteBuffer();
        }
    }

    // Process queued messages
    processQueue();
}

bool BitchatBLEService::broadcastMessage(const BitchatMessage& msg) {
    if (!_serviceActive) {
        return false;
    }

    // Serialize message
    uint8_t buffer[BITCHAT_MAX_MESSAGE_SIZE];
    size_t len = BitchatProtocol::serializeMessage(msg, buffer, sizeof(buffer));
    if (len == 0) {
        return false;
    }

    // Send via notify
    if (_characteristic.notify(buffer, len)) {
        BITCHAT_DEBUG_PRINTLN("TX: type=0x%02X, len=%zu", msg.type, len);
        return true;
    }

    return false;
}

// Static callbacks

void BitchatBLEService::onConnect(uint16_t conn_handle) {
    if (_instance != nullptr) {
        _instance->_bitchatClientCount++;
        _instance->_pendingConnect = true;
        BITCHAT_DEBUG_PRINTLN("BLE client connected");
    }
}

void BitchatBLEService::onDisconnect(uint16_t conn_handle, uint8_t reason) {
    if (_instance != nullptr) {
        _instance->onServerDisconnect();
        BITCHAT_DEBUG_PRINTLN("BLE client disconnected, reason=0x%02X", reason);
    }
}

void BitchatBLEService::onCharacteristicWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (_instance == nullptr || len == 0) {
        return;
    }

    _instance->_lastWriteTime = millis();
    _instance->_pendingData = true;

    // Append to write buffer
    size_t copyLen = len;
    if (_instance->_writeBufferOffset + copyLen > sizeof(_instance->_writeBuffer)) {
        _instance->clearWriteBuffer();
        copyLen = (len > sizeof(_instance->_writeBuffer)) ? sizeof(_instance->_writeBuffer) : len;
    }

    memcpy(&_instance->_writeBuffer[_instance->_writeBufferOffset], data, copyLen);
    _instance->_writeBufferOffset += copyLen;
}

void BitchatBLEService::onCharacteristicCccdWrite(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value) {
    if (_instance != nullptr) {
        _instance->_clientSubscribed = (cccd_value & BLE_GATT_HVX_NOTIFICATION) != 0;
        BITCHAT_DEBUG_PRINTLN("CCCD write: notifications %s", _instance->_clientSubscribed ? "enabled" : "disabled");
    }
}

#endif // NRF52_PLATFORM
