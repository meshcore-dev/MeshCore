#pragma once

#ifdef NRF52_PLATFORM

#include "../dogechat/DogechatProtocol.h"
#include <bluefruit.h>

/**
 * Callback interface for Dogechat BLE events
 */
class DogechatBLECallback {
public:
    virtual ~DogechatBLECallback() {}

    /**
     * Called when a Dogechat message is received via BLE
     * @param msg The received message
     */
    virtual void onDogechatMessageReceived(const DogechatMessage& msg) = 0;

    /**
     * Called when a Dogechat BLE client connects
     */
    virtual void onDogechatClientConnect() {}

    /**
     * Called when a Dogechat BLE client disconnects
     */
    virtual void onDogechatClientDisconnect() {}
};

/**
 * Dogechat BLE Service for NRF52 (using Bluefruit)
 * Provides a GATT service for Dogechat protocol communication
 *
 * Note: This uses standalone BLE advertising. For NRF52, when Dogechat is enabled,
 * the MeshCore companion uses USB serial while Dogechat uses BLE independently.
 */
class DogechatBLEService {
public:
    DogechatBLEService();

    /**
     * Initialize BLE and start the Dogechat service in standalone mode
     * Creates BLE server with Dogechat service only.
     * @param deviceName BLE device name for advertising
     * @param callback Callback for Dogechat events
     * @return true if successful
     */
    bool beginStandalone(const char* deviceName, DogechatBLECallback* callback);

    /**
     * Start BLE advertising
     * Call after beginStandalone() to start advertising
     */
    void startAdvertising();

    /**
     * Check if the service is active
     */
    bool isActive() const { return _serviceActive; }

    /**
     * Check if a Dogechat client is connected
     */
    bool hasConnectedClient() const { return _dogechatClientCount > 0; }

    /**
     * Broadcast a message to connected Dogechat clients
     * @param msg Message to send
     * @return true if message was sent
     */
    bool broadcastMessage(const DogechatMessage& msg);

    /**
     * Process loop - call from main loop
     * Handles deferred message processing
     */
    void loop();

    /**
     * Called when a client disconnects
     */
    void onServerDisconnect();

private:
    // Bluefruit service and characteristic
    BLEService _service;
    BLECharacteristic _characteristic;
    DogechatBLECallback* _callback;
    char _deviceName[48];

    bool _serviceActive;
    uint8_t _dogechatClientCount;
    bool _clientSubscribed;

    // Flags for deferred processing
    volatile bool _pendingConnect;
    volatile bool _pendingData;

    // Write buffer for reassembling fragmented BLE writes
    uint8_t _writeBuffer[512];
    size_t _writeBufferOffset;
    uint32_t _lastWriteTime;
    static const uint32_t WRITE_TIMEOUT_MS = 5000;

    // Message queue for deferred processing (incoming)
    static const size_t MESSAGE_QUEUE_SIZE = 8;
    struct QueuedMessage {
        DogechatMessage msg;
        bool valid;
    };
    QueuedMessage _messageQueue[MESSAGE_QUEUE_SIZE];
    volatile size_t _queueHead;
    volatile size_t _queueTail;

    // Pending outgoing message (when client not yet subscribed)
    DogechatMessage _pendingOutgoing;
    bool _hasPendingOutgoing;

    void clearWriteBuffer();
    bool queueMessage(const DogechatMessage& msg);
    void processQueue();
    void sendPendingOutgoing();

    // Static callbacks for Bluefruit (will call instance methods)
    static void onConnect(uint16_t conn_handle);
    static void onDisconnect(uint16_t conn_handle, uint8_t reason);
    static void onCharacteristicWrite(uint16_t conn_handle, BLECharacteristic* chr, uint8_t* data, uint16_t len);
    static void onCharacteristicCccdWrite(uint16_t conn_handle, BLECharacteristic* chr, uint16_t cccd_value);

    // Singleton for static callback access
    static DogechatBLEService* _instance;
};

#endif // NRF52_PLATFORM
