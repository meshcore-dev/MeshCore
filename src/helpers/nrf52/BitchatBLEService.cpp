#include "DogechatBLEService.h"

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
#if DOGECHAT_DEBUG
  #define DOGECHAT_DEBUG_PRINTLN(...) do { Serial.printf("DOGECHAT_BLE: "); Serial.printf(__VA_ARGS__); Serial.println(); } while(0)
#else
  #define DOGECHAT_DEBUG_PRINTLN(...) {}
#endif

// Dogechat service UUID: F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C
// Bluefruit uses little-endian byte order for UUIDs
static const uint8_t DOGECHAT_SERVICE_UUID_BYTES[] = {
    0x5C, 0x4B, 0x3A, 0x2C, 0x1D, 0x8E, 0x3F, 0x9B,
    0x5A, 0x4C, 0x9E, 0x4A, 0x2D, 0x5E, 0x7B, 0xF4
};

// Dogechat characteristic UUID: A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D
// (Same as ESP32 DOGECHAT_CHARACTERISTIC_UUID in DogechatProtocol.h)
// Bluefruit uses little-endian byte order for UUIDs
static const uint8_t DOGECHAT_CHARACTERISTIC_UUID_BYTES[] = {
    0x5D, 0x4C, 0x3B, 0x2A, 0x1F, 0x0E, 0x9D, 0x8C,
    0x5B, 0x4A, 0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1
};

// Singleton instance for static callback access
DogechatBLEService* DogechatBLEService::_instance = nullptr;

DogechatBLEService::DogechatBLEService()
    : _service(DOGECHAT_SERVICE_UUID_BYTES)
    , _characteristic(DOGECHAT_CHARACTERISTIC_UUID_BYTES)
    , _callback(nullptr)
    , _serviceActive(false)
    , _dogechatClientCount(0)
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
    strcpy(_deviceName, "Dogechat");
    for (size_t i = 0; i < MESSAGE_QUEUE_SIZE; i++) {
        _messageQueue[i].valid = false;
    }
    memset(&_pendingOutgoing, 0, sizeof(_pendingOutgoing));
    _instance = this;
}

bool DogechatBLEService::beginStandalone(const char* deviceName, DogechatBLECallback* callback) {
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

    // Dogechat uses open security (no PIN required)
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
    if (j == 0) strcpy(safeName, "Dogechat");

    Bluefruit.setName(safeName);

    // Configure the Dogechat service
    _service.begin();

    // Configure the characteristic with READ, WRITE, WRITE_NR, NOTIFY properties
    _characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP | CHR_PROPS_NOTIFY | CHR_PROPS_INDICATE);
    _characteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);  // Open security
    _characteristic.setMaxLen(512);  // Support large Dogechat messages
    _characteristic.setWriteCallback(onCharacteristicWrite);
    _characteristic.setCccdWriteCallback(onCharacteristicCccdWrite);
    _characteristic.begin();

    _serviceActive = true;
    DOGECHAT_DEBUG_PRINTLN("Dogechat BLE service initialized: %s", safeName);

    return true;
}

void DogechatBLEService::startAdvertising() {
    // Clear any previous advertising data
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();

    // Set Dogechat UUID in MAIN advertisement (required for Dogechat app discovery)
    // The Dogechat Android app filters on service UUID in main advertisement packet
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(_service);

    // Put device name in scan response (not main adv - no room with 128-bit UUID)
    Bluefruit.ScanResponse.addName();

    // Configure advertising parameters
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);    // in units of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);      // seconds in fast mode
    Bluefruit.Advertising.start(0);                // 0 = Don't stop advertising

    DOGECHAT_DEBUG_PRINTLN("BLE advertising started");
}

void DogechatBLEService::onServerDisconnect() {
    if (_dogechatClientCount > 0) {
        _dogechatClientCount--;
    }

    if (_dogechatClientCount == 0) {
        _clientSubscribed = false;
        clearWriteBuffer();
        if (_callback != nullptr) {
            _callback->onDogechatClientDisconnect();
        }
    }
}

void DogechatBLEService::clearWriteBuffer() {
    _writeBufferOffset = 0;
    memset(_writeBuffer, 0, sizeof(_writeBuffer));
}

bool DogechatBLEService::queueMessage(const DogechatMessage& msg) {
    size_t nextTail = (_queueTail + 1) % MESSAGE_QUEUE_SIZE;

    if (nextTail == _queueHead) {
        DOGECHAT_DEBUG_PRINTLN("Message queue full, dropping message");
        return false;
    }

    _messageQueue[_queueTail].msg = msg;
    _messageQueue[_queueTail].valid = true;
    _queueTail = nextTail;

    return true;
}

void DogechatBLEService::processQueue() {
    while (_queueHead != _queueTail) {
        if (_messageQueue[_queueHead].valid) {
            _messageQueue[_queueHead].valid = false;

            if (_callback != nullptr) {
                Serial.println("BLE_SERVICE: processQueue() calling callback...");
                _callback->onDogechatMessageReceived(_messageQueue[_queueHead].msg);
                Serial.println("BLE_SERVICE: processQueue() callback returned");
            }
        }
        _queueHead = (_queueHead + 1) % MESSAGE_QUEUE_SIZE;
    }
}

void DogechatBLEService::loop() {
    uint32_t now = millis();

    // Handle deferred connect callback
    if (_pendingConnect) {
        _pendingConnect = false;
        if (_callback != nullptr) {
            _callback->onDogechatClientConnect();
        }
    }

    // Handle deferred data processing
    // Wait 100ms after last write before processing to allow multi-chunk messages to arrive
    if (_pendingData && (now - _lastWriteTime >= 100)) {
        _pendingData = false;
        DOGECHAT_DEBUG_PRINTLN("Processing %u buffered bytes, first bytes: %02X %02X %02X %02X",
            (unsigned)_writeBufferOffset,
            _writeBufferOffset > 0 ? _writeBuffer[0] : 0,
            _writeBufferOffset > 1 ? _writeBuffer[1] : 0,
            _writeBufferOffset > 2 ? _writeBuffer[2] : 0,
            _writeBufferOffset > 3 ? _writeBuffer[3] : 0);

        DogechatMessage msg;
        if (DogechatProtocol::parseMessage(_writeBuffer, _writeBufferOffset, msg)) {
            if (DogechatProtocol::validateMessage(msg)) {
                DOGECHAT_DEBUG_PRINTLN("Received Dogechat message: type=%02X, len=%d", msg.type, msg.payloadLength);
                queueMessage(msg);
            } else {
                DOGECHAT_DEBUG_PRINTLN("Invalid Dogechat message received (validation failed)");
            }
            clearWriteBuffer();
        } else {
            DOGECHAT_DEBUG_PRINTLN("Parse failed, have %u bytes, need more or invalid data", (unsigned)_writeBufferOffset);
            if (_writeBufferOffset >= DOGECHAT_HEADER_SIZE) {
                size_t expectedMin = DogechatProtocol::getMessageSize(msg);
                DOGECHAT_DEBUG_PRINTLN("Expected min size: %u", (unsigned)expectedMin);
                if (_writeBufferOffset > expectedMin + 100) {
                    DOGECHAT_DEBUG_PRINTLN("Write buffer contains unparseable data, clearing");
                    clearWriteBuffer();
                }
            }
        }
    }

    // Check for write buffer timeout
    if (_writeBufferOffset > 0) {
        if (now - _lastWriteTime > WRITE_TIMEOUT_MS) {
            DOGECHAT_DEBUG_PRINTLN("Write buffer timeout, clearing");
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

bool DogechatBLEService::broadcastMessage(const DogechatMessage& msg) {
    Serial.println("BLE_SERVICE: >>> broadcastMessage() ENTRY <<<");
    DOGECHAT_DEBUG_PRINTLN("broadcastMessage: type=0x%02X, active=%d, subscribed=%d, clients=%d",
                          msg.type, _serviceActive, _clientSubscribed, _dogechatClientCount);

    if (!_serviceActive) {
        DOGECHAT_DEBUG_PRINTLN("broadcastMessage: service not active");
        return false;
    }

    // Serialize message - use static buffer to avoid stack overflow
    static uint8_t buffer[DOGECHAT_MAX_MESSAGE_SIZE];
    size_t len = DogechatProtocol::serializeMessage(msg, buffer, sizeof(buffer));
    if (len == 0) {
        DOGECHAT_DEBUG_PRINTLN("broadcastMessage: serialize failed");
        return false;
    }

    // Always set the characteristic value so it can be read
    _characteristic.write(buffer, len);
    DOGECHAT_DEBUG_PRINTLN("broadcastMessage: set characteristic value (%u bytes)", (unsigned)len);

    // If client connected but not yet subscribed, queue for notification later
    if (_dogechatClientCount > 0 && !_clientSubscribed) {
        DOGECHAT_DEBUG_PRINTLN("broadcastMessage: client not subscribed, queuing notify for later");
        _pendingOutgoing = msg;
        _hasPendingOutgoing = true;
        return true;  // Value is set for reading, notify will happen when they subscribe
    }

    // Try to send via notify if we have subscribers
    if (_clientSubscribed) {
        Serial.println("BLE_SERVICE: Client subscribed, sending notify");
        uint16_t result = _characteristic.notify(buffer, len);
        DOGECHAT_DEBUG_PRINTLN("broadcastMessage: notify returned %u", result);
        if (result) {
            DOGECHAT_DEBUG_PRINTLN("TX: type=0x%02X, len=%u", msg.type, (unsigned)len);
        } else {
            Serial.println("BLE_SERVICE: WARNING - notify returned 0 (failed)");
        }
    } else {
        Serial.println("BLE_SERVICE: Client NOT subscribed, only set characteristic value");
    }

    return true;
}

void DogechatBLEService::sendPendingOutgoing() {
    if (!_hasPendingOutgoing || !_clientSubscribed) {
        return;
    }

    DOGECHAT_DEBUG_PRINTLN("Sending pending outgoing message");
    _hasPendingOutgoing = false;

    // Serialize and send the pending message - use static buffer to avoid stack overflow
    static uint8_t buffer[DOGECHAT_MAX_MESSAGE_SIZE];
    size_t len = DogechatProtocol::serializeMessage(_pendingOutgoing, buffer, sizeof(buffer));
    if (len > 0) {
        uint16_t result = _characteristic.notify(buffer, len);
        DOGECHAT_DEBUG_PRINTLN("Pending message notify returned %u", result);
    }
}

// Static callbacks

void DogechatBLEService::onConnect(uint16_t conn_handle) {
    if (_instance != nullptr) {
        _instance->_dogechatClientCount++;
        _instance->_pendingConnect = true;
        DOGECHAT_DEBUG_PRINTLN("BLE client connected");
    }
}

void DogechatBLEService::onDisconnect(uint16_t conn_handle, uint8_t reason) {
    if (_instance != nullptr) {
        _instance->onServerDisconnect();
        DOGECHAT_DEBUG_PRINTLN("BLE client disconnected, reason=0x%02X", reason);
    }
}

void DogechatBLEService::onCharacteristicWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    DOGECHAT_DEBUG_PRINTLN("BLE WRITE received: %u bytes", len);

    if (_instance == nullptr || len == 0) {
        return;
    }

    _instance->_lastWriteTime = millis();
    _instance->_pendingData = true;

    // Append to write buffer
    size_t copyLen = len;
    if (_instance->_writeBufferOffset + copyLen > sizeof(_instance->_writeBuffer)) {
        DOGECHAT_DEBUG_PRINTLN("Write buffer overflow, clearing");
        _instance->clearWriteBuffer();
        copyLen = (len > sizeof(_instance->_writeBuffer)) ? sizeof(_instance->_writeBuffer) : len;
    }

    memcpy(&_instance->_writeBuffer[_instance->_writeBufferOffset], data, copyLen);
    _instance->_writeBufferOffset += copyLen;
    DOGECHAT_DEBUG_PRINTLN("Write buffer now has %u bytes", (unsigned)_instance->_writeBufferOffset);
}

void DogechatBLEService::onCharacteristicCccdWrite(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value) {
    Serial.print("BLE_SERVICE: CCCD write callback, cccd_value=0x");
    Serial.println(cccd_value, HEX);

    if (_instance != nullptr) {
        bool wasSubscribed = _instance->_clientSubscribed;
        _instance->_clientSubscribed = (cccd_value & BLE_GATT_HVX_NOTIFICATION) != 0;
        DOGECHAT_DEBUG_PRINTLN("CCCD write: notifications %s (was %s)",
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
