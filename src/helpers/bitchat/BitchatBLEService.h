#pragma once

#include "BitchatProtocol.h"

#ifdef ESP32
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>

/**
 * Callback interface for Bitchat BLE events
 */
class BitchatBLECallback {
public:
    virtual ~BitchatBLECallback() {}

    /**
     * Called when a Bitchat message is received via BLE
     * @param msg The received message
     */
    virtual void onBitchatMessageReceived(const BitchatMessage& msg) = 0;

    /**
     * Called when a Bitchat BLE client connects
     */
    virtual void onBitchatClientConnect() {}

    /**
     * Called when a Bitchat BLE client disconnects
     */
    virtual void onBitchatClientDisconnect() {}
};

/**
 * Bitchat BLE Service
 * Provides a GATT service for Bitchat protocol communication
 * Can be attached to an existing BLE server
 */
class BitchatBLEService : public BLECharacteristicCallbacks {
public:
    BitchatBLEService();

    /**
     * Attach to an existing BLE server
     * Must be called after BLEDevice::init() and server creation
     * @param server The BLE server to attach to
     * @param callback Callback for Bitchat events
     * @return true if service was created successfully
     */
    bool attachToServer(BLEServer* server, BitchatBLECallback* callback);

    /**
     * Start the Bitchat service (shared BLE mode)
     * Sets Bitchat UUID in scan response for coexistence with MeshCore UUID
     * Call after attachToServer() and before advertising
     */
    void start();

    /**
     * Start the Bitchat service only, without touching advertising
     * Use this for standalone mode where startAdvertising() will be called separately
     */
    void startServiceOnly();

    /**
     * Set the device name for BLE advertising
     * Call before startAdvertising()
     */
    void setDeviceName(const char* name);

    /**
     * Start BLE advertising with Bitchat UUID in main advertisement
     * This is required for Bitchat app discovery (it filters on main adv UUID)
     */
    void startAdvertising();

    /**
     * Check if the service is active
     */
    bool isActive() const { return _serviceActive; }

    /**
     * Check if a Bitchat client is connected
     * Note: This tracks clients that have interacted with the Bitchat characteristic
     */
    bool hasConnectedClient() const { return _bitchatClientCount > 0; }

    /**
     * Broadcast a message to connected Bitchat clients
     * @param msg Message to send
     * @return true if message was sent
     */
    bool broadcastMessage(const BitchatMessage& msg);

    /**
     * Process loop - call from main loop
     * Handles deferred message processing
     */
    void loop();

    /**
     * Mark client as disconnected (call from server disconnect callback)
     */
    void onServerDisconnect();

protected:
    // BLECharacteristicCallbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;
    void onRead(BLECharacteristic* pCharacteristic) override;
    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) override;

private:
    BLEServer* _server;
    BLEService* _service;
    BLECharacteristic* _characteristic;
    BitchatBLECallback* _callback;
    char _deviceName[48];

    bool _serviceActive;
    uint8_t _bitchatClientCount;      // Number of Bitchat clients that have written
    uint8_t _lastKnownServerCount;    // Track server's total connection count
    bool _clientSubscribed;  // True when client has subscribed to notifications

    // Flags for deferred processing (to avoid work in BLE callbacks)
    volatile bool _pendingConnect;  // Client connected, callback not yet called
    volatile bool _pendingData;     // Data received, not yet parsed

    // Write buffer for reassembling fragmented BLE writes
    // Size 512 to handle padded Bitchat messages (Android pads to 256 or 512 bytes)
    uint8_t _writeBuffer[512];
    size_t _writeBufferOffset;
    uint32_t _lastWriteTime;
    static const uint32_t WRITE_TIMEOUT_MS = 5000;

    // Message queue for deferred processing (to avoid processing in BLE callback)
    static const size_t MESSAGE_QUEUE_SIZE = 8;
    struct QueuedMessage {
        BitchatMessage msg;
        bool valid;
    };
    QueuedMessage _messageQueue[MESSAGE_QUEUE_SIZE];
    volatile size_t _queueHead;
    volatile size_t _queueTail;

    void clearWriteBuffer();
    bool queueMessage(const BitchatMessage& msg);
    void processQueue();
    void checkForDisconnects();

};

#endif // ESP32
