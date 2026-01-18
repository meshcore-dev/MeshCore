#include "BitchatBLEService.h"

#ifdef NRF52_PLATFORM

#include <Arduino.h>

// HardFault handler - catches crashes and prints debug info
extern "C" {
  void HardFault_Handler(void) {
    Serial.println("\n\n!!! HARDFAULT DETECTED !!!");
    Serial.println("Crash - likely stack overflow or memory corruption");
    Serial.flush();

    // Blink LED rapidly to indicate crash
    #ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    #endif

    while(1) {
      #ifdef LED_BUILTIN
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
      #else
      delay(200);
      #endif
    }
  }
}

// Debug output - Adafruit nRF52 core supports Serial.printf
#if BITCHAT_DEBUG
  #define BITCHAT_DEBUG_PRINTLN(...) do { Serial.printf("BITCHAT_BLE: "); Serial.printf(__VA_ARGS__); Serial.println(); } while(0)
#else
  #define BITCHAT_DEBUG_PRINTLN(...) {}
#endif

// Bitchat service UUID: F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C
// Bluefruit uses little-endian byte order for UUIDs
static const uint8_t BITCHAT_SERVICE_UUID_BYTES[] = {
    0x5C, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B,
    0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4
};

// Bitchat characteristic UUID: A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D
// (Same as ESP32 BITCHAT_CHARACTERISTIC_UUID in BitchatProtocol.h)
// Bluefruit uses little-endian byte order for UUIDs
static const uint8_t BITCHAT_CHARACTERISTIC_UUID_BYTES[] = {
    0x5D, 0x4C, 0x3B, 0x2A, 0x1F, 0x0E, 0x9D, 0x8C,
    0x5B, 0x4A, 0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1
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
    , _hasPendingOutgoing(false)
{
    memset(_writeBuffer, 0, sizeof(_writeBuffer));
    memset(_deviceName, 0, sizeof(_deviceName));
    strcpy(_deviceName, "Bitchat");
    for (size_t i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        _messageQueue[i].valid = false;
    }
    memset(&_pendingOutgoing, 0, sizeof(_pendingOutgoing));
    _instance = this;
}

bool BitchatBLEService::beginStandalone(const char* deviceName, BitchatBLECallback* callback) {
    if (callback == nullptr) {
        return false;
    }

    _callback = callback;
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
    _deviceName[sizeof(_deviceName) - 1] = '\0';

    // Configure connection parameters BEFORE begin() - set MTU to 517 (max BLE MTU)
    // This matches ESP32's BLEDevice::setMTU(517)
    // Parameters: mtu_max, event_len, hvn_qsize, wrcmd_qsize
    Bluefruit.configPrphConn(517, BLE_GAP_EVENT_LENGTH_DEFAULT, BLE_GATTS_HVN_TX_QUEUE_SIZE_DEFAULT, BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT);

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
    _characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY | CHR_PROPS_INDICATE);
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
                Serial.println("BLE_SERVICE: processQueue() calling callback...");
                _callback->onBitchatMessageReceived(_messageQueue[_queueHead].msg);
                Serial.println("BLE_SERVICE: processQueue() callback returned");
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
        BITCHAT_DEBUG_PRINTLN("Processing %u buffered bytes, first bytes: %02X %02X %02X %02X",
            (unsigned)_writeBufferOffset,
            _writeBufferOffset > 0 ? _writeBuffer[0] : 0,
            _writeBufferOffset > 1 ? _writeBuffer[1] : 0,
            _writeBufferOffset > 2 ? _writeBuffer[2] : 0,
            _writeBufferOffset > 3 ? _writeBuffer[3] : 0);

        BitchatMessage msg;
        if (BitchatProtocol::parseMessage(_writeBuffer, _writeBufferOffset, msg)) {
            if (BitchatProtocol::validateMessage(msg)) {
                BITCHAT_DEBUG_PRINTLN("Received Bitchat message: type=%02X, len=%d", msg.type, msg.payloadLength);
                queueMessage(msg);
            } else {
                BITCHAT_DEBUG_PRINTLN("Invalid Bitchat message received (validation failed)");
            }
            clearWriteBuffer();
        } else {
            BITCHAT_DEBUG_PRINTLN("Parse failed, have %u bytes, need more or invalid data", (unsigned)_writeBufferOffset);
            if (_writeBufferOffset >= BITCHAT_HEADER_SIZE) {
                size_t expectedMin = BitchatProtocol::getMessageSize(msg);
                BITCHAT_DEBUG_PRINTLN("Expected min size: %u", (unsigned)expectedMin);
                if (_writeBufferOffset > expectedMin + 100) {
                    BITCHAT_DEBUG_PRINTLN("Write buffer contains unparseable data, clearing");
                    clearWriteBuffer();
                }
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

    // Debug: mark end of loop iteration if we processed something
    static uint32_t lastLoopPrint = 0;
    if (now - lastLoopPrint > 5000) {
        Serial.println("BLE_SERVICE: loop() heartbeat");
        lastLoopPrint = now;
    }
}

bool BitchatBLEService::broadcastMessage(const BitchatMessage& msg) {
    Serial.println("BLE_SERVICE: >>> broadcastMessage() ENTRY <<<");
    BITCHAT_DEBUG_PRINTLN("broadcastMessage: type=0x%02X, active=%d, subscribed=%d, clients=%d",
                          msg.type, _serviceActive, _clientSubscribed, _bitchatClientCount);

    if (!_serviceActive) {
        BITCHAT_DEBUG_PRINTLN("broadcastMessage: service not active");
        return false;
    }

    // Serialize message - use static buffer to avoid stack overflow
    static uint8_t buffer[BITCHAT_MAX_MESSAGE_SIZE];
    size_t len = BitchatProtocol::serializeMessage(msg, buffer, sizeof(buffer));
    if (len == 0) {
        BITCHAT_DEBUG_PRINTLN("broadcastMessage: serialize failed");
        return false;
    }

    // Always set the characteristic value so it can be read
    _characteristic.write(buffer, len);
    BITCHAT_DEBUG_PRINTLN("broadcastMessage: set characteristic value (%u bytes)", (unsigned)len);

    // If client connected but not yet subscribed, queue for notification later
    if (_bitchatClientCount > 0 && !_clientSubscribed) {
        BITCHAT_DEBUG_PRINTLN("broadcastMessage: client not subscribed, queuing notify for later");
        _pendingOutgoing = msg;
        _hasPendingOutgoing = true;
        return true;  // Value is set for reading, notify will happen when they subscribe
    }

    // Try to send via notify if we have subscribers
    if (_clientSubscribed) {
        Serial.println("BLE_SERVICE: Client subscribed, sending notify");
        uint16_t result = _characteristic.notify(buffer, len);
        BITCHAT_DEBUG_PRINTLN("broadcastMessage: notify returned %u", result);
        if (result) {
            BITCHAT_DEBUG_PRINTLN("TX: type=0x%02X, len=%u", msg.type, (unsigned)len);
        } else {
            Serial.println("BLE_SERVICE: WARNING - notify returned 0 (failed)");
        }
    } else {
        Serial.println("BLE_SERVICE: Client NOT subscribed, only set characteristic value");
    }

    return true;
}

void BitchatBLEService::sendPendingOutgoing() {
    if (!_hasPendingOutgoing || !_clientSubscribed) {
        return;
    }

    BITCHAT_DEBUG_PRINTLN("Sending pending outgoing message");
    _hasPendingOutgoing = false;

    // Serialize and send the pending message - use static buffer to avoid stack overflow
    static uint8_t buffer[BITCHAT_MAX_MESSAGE_SIZE];
    size_t len = BitchatProtocol::serializeMessage(_pendingOutgoing, buffer, sizeof(buffer));
    if (len > 0) {
        uint16_t result = _characteristic.notify(buffer, len);
        BITCHAT_DEBUG_PRINTLN("Pending message notify returned %u", result);
    }
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
    BITCHAT_DEBUG_PRINTLN("BLE WRITE received: %u bytes", len);

    if (_instance == nullptr || len == 0) {
        return;
    }

    _instance->_lastWriteTime = millis();
    _instance->_pendingData = true;

    // Append to write buffer
    size_t copyLen = len;
    if (_instance->_writeBufferOffset + copyLen > sizeof(_instance->_writeBuffer)) {
        BITCHAT_DEBUG_PRINTLN("Write buffer overflow, clearing");
        _instance->clearWriteBuffer();
        copyLen = (len > sizeof(_instance->_writeBuffer)) ? sizeof(_instance->_writeBuffer) : len;
    }

    memcpy(&_instance->_writeBuffer[_instance->_writeBufferOffset], data, copyLen);
    _instance->_writeBufferOffset += copyLen;
    BITCHAT_DEBUG_PRINTLN("Write buffer now has %u bytes", (unsigned)_instance->_writeBufferOffset);
}

void BitchatBLEService::onCharacteristicCccdWrite(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value) {
    Serial.print("BLE_SERVICE: CCCD write callback, cccd_value=0x");
    Serial.println(cccd_value, HEX);

    if (_instance != nullptr) {
        bool wasSubscribed = _instance->_clientSubscribed;
        _instance->_clientSubscribed = (cccd_value & BLE_GATT_HVX_NOTIFICATION) != 0;
        BITCHAT_DEBUG_PRINTLN("CCCD write: notifications %s (was %s)",
                              _instance->_clientSubscribed ? "enabled" : "disabled",
                              wasSubscribed ? "enabled" : "disabled");
        Serial.print("BLE_SERVICE: _clientSubscribed now = ");
        Serial.println(_instance->_clientSubscribed ? "true" : "false");

        // If client just subscribed and we have pending messages, send them
        if (!wasSubscribed && _instance->_clientSubscribed) {
            Serial.println("BLE_SERVICE: Client just subscribed, sending pending");
            _instance->sendPendingOutgoing();
        }
    }
}

#endif // NRF52_PLATFORM
