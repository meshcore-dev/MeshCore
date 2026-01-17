#include "BitchatBLEService.h"

#ifdef ESP32

#include <Arduino.h>

#if BITCHAT_DEBUG
  #define BITCHAT_DEBUG_PRINTLN(F, ...) Serial.printf("BITCHAT: " F "\n", ##__VA_ARGS__)
#else
  #define BITCHAT_DEBUG_PRINTLN(...) {}
#endif

BitchatBLEService::BitchatBLEService()
    : _server(nullptr)
    , _service(nullptr)
    , _characteristic(nullptr)
    , _callback(nullptr)
    , _serviceActive(false)
    , _bitchatClientCount(0)
    , _lastKnownServerCount(0)
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
}

bool BitchatBLEService::attachToServer(BLEServer* server, BitchatBLECallback* callback) {
    if (server == nullptr || callback == nullptr) {
        BITCHAT_DEBUG_PRINTLN("attachToServer: null server or callback");
        return false;
    }

    _server = server;
    _callback = callback;

    // Create Bitchat service
    _service = _server->createService(BITCHAT_SERVICE_UUID);
    if (_service == nullptr) {
        BITCHAT_DEBUG_PRINTLN("Failed to create Bitchat service");
        return false;
    }

    // Create characteristic with READ, WRITE, WRITE_NR, NOTIFY, and INDICATE properties
    _characteristic = _service->createCharacteristic(
        BITCHAT_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_INDICATE
    );

    if (_characteristic == nullptr) {
        BITCHAT_DEBUG_PRINTLN("Failed to create Bitchat characteristic");
        return false;
    }

    // Bitchat uses open security (no PIN required)
    _characteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);

    // Add descriptor for notifications
    _characteristic->addDescriptor(new BLE2902());

    // Set callbacks
    _characteristic->setCallbacks(this);

    BITCHAT_DEBUG_PRINTLN("Bitchat BLE service attached to server");
    return true;
}

// MeshCore UART service UUID (for scan response)
#define MESHCORE_UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

void BitchatBLEService::start() {
    if (_service == nullptr) {
        BITCHAT_DEBUG_PRINTLN("Cannot start: service not created");
        return;
    }

    // Request larger MTU to support Bitchat messages (up to 512 bytes padded)
    // This overrides the default MAX_FRAME_SIZE (172) used by MeshCore
    BLEDevice::setMTU(517);  // Max BLE MTU

    _service->start();
    _serviceActive = true;

    // NOTE: In shared BLE mode (with SerialBLEInterface), we set Bitchat UUID in scan response
    // to coexist with MeshCore UUID in main advertisement.
    // In standalone mode, caller should use startAdvertising() instead which puts
    // Bitchat UUID in main advertisement (required for Bitchat app discovery).
    if (_server != nullptr) {
        BLEAdvertising* advertising = _server->getAdvertising();

        // Set scan response data (will be used when advertising starts)
        BLEAdvertisementData scanResponse;
        scanResponse.setCompleteServices(BLEUUID(BITCHAT_SERVICE_UUID));
        advertising->setScanResponseData(scanResponse);

        BITCHAT_DEBUG_PRINTLN("Bitchat BLE service started (shared mode)");
    } else {
        BITCHAT_DEBUG_PRINTLN("Bitchat BLE service started");
    }
}

void BitchatBLEService::startServiceOnly() {
    if (_service == nullptr) {
        return;
    }

    // Request larger MTU to support Bitchat messages (up to 512 bytes padded)
    BLEDevice::setMTU(517);

    _service->start();
    _serviceActive = true;
    BITCHAT_DEBUG_PRINTLN("Bitchat BLE service started (standalone)");
}

void BitchatBLEService::setDeviceName(const char* name) {
    strncpy(_deviceName, name, sizeof(_deviceName) - 1);
    _deviceName[sizeof(_deviceName) - 1] = '\0';
}

void BitchatBLEService::startAdvertising() {
    if (_server == nullptr) {
        return;
    }

    BLEAdvertising* advertising = _server->getAdvertising();

    // Set Bitchat UUID in MAIN advertisement (required for Bitchat app discovery)
    // The Bitchat Android app filters on service UUID in main advertisement packet
    // BLE advertisement packet is max 31 bytes:
    //   Flags: 3 bytes, 128-bit UUID: 18 bytes = 21 bytes used
    //   Remaining for name: 10 bytes (2 header + 8 chars max)
    // Put full name in scan response instead
    BLEAdvertisementData advData;
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    advData.setCompleteServices(BLEUUID(BITCHAT_SERVICE_UUID));
    // Don't set name in main adv - no room with 128-bit UUID
    advertising->setAdvertisementData(advData);

    // Put device name in scan response (ASCII only, no emoji - they break BLE)
    char safeName[20];
    size_t j = 0;
    for (size_t i = 0; _deviceName[i] != '\0' && j < sizeof(safeName) - 1; i++) {
        // Only copy ASCII printable characters (skip emoji/unicode)
        if (_deviceName[i] >= 0x20 && _deviceName[i] <= 0x7E) {
            safeName[j++] = _deviceName[i];
        }
    }
    safeName[j] = '\0';
    if (j == 0) strcpy(safeName, "Bitchat");  // Fallback if name was all emoji
    BLEAdvertisementData scanResponse;
    scanResponse.setName(safeName);
    advertising->setScanResponseData(scanResponse);

    advertising->start();
    BITCHAT_DEBUG_PRINTLN("BLE advertising started: %s", safeName);
}

void BitchatBLEService::onServerDisconnect() {
    checkForDisconnects();
}

void BitchatBLEService::clearWriteBuffer() {
    _writeBufferOffset = 0;
    memset(_writeBuffer, 0, sizeof(_writeBuffer));
}

void BitchatBLEService::checkForDisconnects() {
    if (_server == nullptr) return;

    uint32_t currentServerCount = _server->getConnectedCount();
    if (currentServerCount < _lastKnownServerCount) {
        uint8_t disconnected = _lastKnownServerCount - currentServerCount;
        _bitchatClientCount = (disconnected >= _bitchatClientCount)
            ? 0 : _bitchatClientCount - disconnected;
        _lastKnownServerCount = currentServerCount;

        if (_bitchatClientCount == 0) {
            _clientSubscribed = false;
            clearWriteBuffer();
            if (_callback != nullptr) {
                _callback->onBitchatClientDisconnect();
            }
        }
    }
}

bool BitchatBLEService::queueMessage(const BitchatMessage& msg) {
    size_t nextTail = (_queueTail + 1) % MESSAGE_QUEUE_SIZE;

    // Check if queue is full
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
    // Check for disconnections (detect when clients drop without callback)
    checkForDisconnects();

    uint32_t now = millis();

    // Handle deferred connect callback (from BLE callback)
    if (_pendingConnect) {
        _pendingConnect = false;
        if (_callback != nullptr) {
            _callback->onBitchatClientConnect();
        }
    }

    // Handle deferred data processing (parsing moved out of BLE callback)
    // Wait 100ms after last write before processing to allow multi-chunk messages to arrive
    if (_pendingData && (now - _lastWriteTime >= 100)) {
        _pendingData = false;
        BITCHAT_DEBUG_PRINTLN("Processing %zu buffered bytes", _writeBufferOffset);

        // Try to parse as complete message
        BitchatMessage msg;
        if (BitchatProtocol::parseMessage(_writeBuffer, _writeBufferOffset, msg)) {
            // Successfully parsed - validate and queue
            if (BitchatProtocol::validateMessage(msg)) {
                BITCHAT_DEBUG_PRINTLN("Received Bitchat message: type=%02X, len=%d", msg.type, msg.payloadLength);
                queueMessage(msg);
            } else {
                BITCHAT_DEBUG_PRINTLN("Invalid Bitchat message received");
            }
            clearWriteBuffer();
        } else if (_writeBufferOffset >= BITCHAT_HEADER_SIZE) {
            // Have enough data to check expected size
            size_t expectedMin = BitchatProtocol::getMessageSize(msg);
            if (_writeBufferOffset > expectedMin + 100) {
                BITCHAT_DEBUG_PRINTLN("Write buffer contains unparseable data, clearing");
                clearWriteBuffer();
            }
            // If parse fails but buffer size is reasonable, keep waiting for more data
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

void BitchatBLEService::onWrite(BLECharacteristic* pCharacteristic) {
    // MINIMAL WORK IN CALLBACK - BLE stack has limited space!
    // Just buffer data and set flags; all processing happens in loop()

    std::string value = pCharacteristic->getValue();
    if (value.empty()) {
        return;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(value.data());
    size_t length = value.length();

    _lastWriteTime = millis();
    _pendingData = true;  // Flag for loop() to process

    // Detect new Bitchat clients by comparing to server's total connection count
    if (_server != nullptr) {
        uint32_t currentServerCount = _server->getConnectedCount();
        if (currentServerCount > _lastKnownServerCount) {
            uint8_t newClients = currentServerCount - _lastKnownServerCount;
            _bitchatClientCount += newClients;
            _lastKnownServerCount = currentServerCount;
            if (newClients > 0) {
                _pendingConnect = true;  // Defer callback to loop()
            }
        }
    }

    // Append to write buffer (increased size for large messages)
    size_t copyLen = length;
    if (_writeBufferOffset + copyLen > sizeof(_writeBuffer)) {
        clearWriteBuffer();
        copyLen = (length > sizeof(_writeBuffer)) ? sizeof(_writeBuffer) : length;
    }

    memcpy(&_writeBuffer[_writeBufferOffset], data, copyLen);
    _writeBufferOffset += copyLen;
}

void BitchatBLEService::onRead(BLECharacteristic* pCharacteristic) {
    // Currently unused - reads return the last written value
    // NO Serial output here - BLE callback has limited stack
}

void BitchatBLEService::onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
    // Called when CCCD is written (client subscribes/unsubscribes to notifications)
    // NO Serial output here - BLE callback has limited stack
    if (s == Status::SUCCESS_NOTIFY || s == Status::SUCCESS_INDICATE) {
        _clientSubscribed = true;
    } else if (s == Status::ERROR_NOTIFY_DISABLED) {
        _clientSubscribed = false;
    }
}

bool BitchatBLEService::broadcastMessage(const BitchatMessage& msg) {
    if (!_serviceActive || _characteristic == nullptr) {
        return false;
    }

    // Serialize message
    uint8_t buffer[BITCHAT_MAX_MESSAGE_SIZE];
    size_t len = BitchatProtocol::serializeMessage(msg, buffer, sizeof(buffer));
    if (len == 0) {
        return false;
    }

    // Set value and notify
    _characteristic->setValue(buffer, len);
    _characteristic->notify(true);

    BITCHAT_DEBUG_PRINTLN("TX: type=0x%02X, len=%zu", msg.type, len);
    return true;
}

#endif // ESP32
