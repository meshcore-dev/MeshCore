#pragma once

#include "DogechatProtocol.h"

#if defined(ESP32)
#include "DogechatBLEService.h"
#elif defined(NRF52_PLATFORM)
#include "../nrf52/DogechatBLEService.h"
#endif

#include <Mesh.h>
#include <Identity.h>

// #mesh channel key: first 16 bytes of SHA256("#mesh")
// This is a "hashtag room" where the key is derived from the channel name
// Calculation: SHA256("#mesh").substring(0, 32) = "5b664cde0b08b220612113db980650f3"
static const uint8_t MESH_CHANNEL_KEY[16] = {
    0x5b, 0x66, 0x4c, 0xde, 0x0b, 0x08, 0xb2, 0x20,
    0x61, 0x21, 0x13, 0xdb, 0x98, 0x06, 0x50, 0xf3
};

// Dogechat channel name for #mesh (includes the # prefix)
#define DOGECHAT_MESH_CHANNEL "#mesh"

/**
 * Dogechat Bridge - Translation layer between Dogechat and Meshcore protocols
 *
 * This bridge relays messages between Dogechat #mesh channel and MeshCore #mesh channel.
 * Only #mesh channel messages are relayed - DMs and other channels are ignored.
 * The #mesh channel uses a hashtag-derived key: SHA256("#mesh")[0:16]
 */
class DogechatBridge
#if defined(ESP32) || defined(NRF52_PLATFORM)
    : public DogechatBLECallback
#endif
{
public:
    /**
     * Constructor
     * @param mesh Reference to the Mesh instance
     * @param identity Reference to this node's LocalIdentity
     * @param nodeName This node's display name
     */
    DogechatBridge(mesh::Mesh& mesh, mesh::LocalIdentity& identity, const char* nodeName);

    /**
     * Initialize the bridge
     * Call after mesh.begin()
     */
    void begin();

    /**
     * Main loop - call from main loop
     */
    void loop();

#if defined(ESP32)
    /**
     * Attach BLE service to existing server (shared BLE mode)
     * @param server BLE server to attach to
     * @return true if successful
     */
    bool attachBLEService(BLEServer* server);

    /**
     * Initialize BLE independently (standalone mode, no SerialBLEInterface)
     * Creates own BLE server with Dogechat service only.
     * Use this when MeshCore companion uses USB serial instead of BLE.
     * @param deviceName BLE device name for advertising
     * @return true if successful
     */
    bool beginStandalone(const char* deviceName);

    /**
     * Get the BLE service for disconnect callback registration
     */
    DogechatBLEService& getBLEService() { return _bleService; }
#elif defined(NRF52_PLATFORM)
    /**
     * Initialize BLE independently (standalone mode)
     * For NRF52, this uses Bluefruit BLE stack.
     * @param deviceName BLE device name for advertising
     * @return true if successful
     */
    bool beginStandalone(const char* deviceName);

    /**
     * Get the BLE service for disconnect callback registration
     */
    DogechatBLEService& getBLEService() { return _bleService; }
#endif

    /**
     * Handle incoming Meshcore GROUP message
     * Call this from onGroupDataRecv() or onChannelMessageRecv()
     * @param channel The Meshcore channel
     * @param timestamp Message timestamp
     * @param senderName Sender's display name (from message)
     * @param text Message text
     */
    void onMeshcoreGroupMessage(const mesh::GroupChannel& channel, uint32_t timestamp,
                                 const char* senderName, const char* text);

    /**
     * Handle incoming Meshcore direct message
     * Call this from onPeerDataRecv()
     * @param senderPubKey Sender's public key
     * @param timestamp Message timestamp
     * @param text Message text
     */
    void onMeshcoreDirectMessage(const uint8_t* senderPubKey, uint32_t timestamp, const char* text);

    /**
     * Handle incoming Meshcore advertisement
     * Call this from onAdvertRecv()
     * @param id Sender's identity
     * @param timestamp Advertisement timestamp
     * @param appData Additional data from advertisement
     * @param appDataLen Length of additional data
     */
    void onMeshcoreAdvert(const mesh::Identity& id, uint32_t timestamp,
                          const uint8_t* appData, size_t appDataLen);

    /**
     * Set the default channel name for Dogechat
     * @param channelName Channel name without # prefix
     */
    void setDefaultChannel(const char* channelName);

    /**
     * Set the channel for outgoing Dogechat messages
     * @param channel Meshcore GroupChannel to use
     */
    void setMeshcoreChannel(const mesh::GroupChannel& channel);

    /**
     * Get this node's Dogechat peer ID (derived from identity)
     */
    uint64_t getDogechatPeerId() const { return _dogechatPeerId; }

    /**
     * Register a channel mapping between Dogechat channel name and MeshCore GroupChannel
     * @param dogechatChannelName Channel name without # prefix (e.g., "general")
     * @param meshChannel MeshCore GroupChannel
     * @return true if mapping was added (false if registry full)
     */
    bool registerChannelMapping(const char* dogechatChannelName, const mesh::GroupChannel& meshChannel);

    /**
     * Find MeshCore channel for a Dogechat channel name
     * @param channelName Channel name (with or without # prefix)
     * @param outChannel OUT: MeshCore GroupChannel if found
     * @return true if mapping found
     */
    bool findMeshChannel(const char* channelName, mesh::GroupChannel& outChannel);

    /**
     * Get Dogechat channel name for a MeshCore channel
     * @param channel MeshCore GroupChannel
     * @return Channel name (without #) or nullptr if not found
     */
    const char* getChannelName(const mesh::GroupChannel& channel);

    /**
     * Check if BLE service is active
     */
    bool isBLEActive() const;

    /**
     * Check if a Dogechat client is connected
     */
    bool hasDogechatClient() const;

    /**
     * Get statistics
     */
    uint32_t getMessagesRelayed() const { return _messagesRelayed; }
    uint32_t getDuplicatesDropped() const { return _duplicatesDropped; }

protected:
#if defined(ESP32) || defined(NRF52_PLATFORM)
    // DogechatBLECallback implementation
    void onDogechatMessageReceived(const DogechatMessage& msg) override;
    void onDogechatClientConnect() override;
    void onDogechatClientDisconnect() override;
#endif

private:
    mesh::Mesh& _mesh;
    mesh::LocalIdentity& _identity;
    const char* _nodeName;

#if defined(ESP32) || defined(NRF52_PLATFORM)
    DogechatBLEService _bleService;
#endif

    DogechatDuplicateCache _duplicateCache;

    // Dogechat peer identity (derived from Meshcore identity)
    uint64_t _dogechatPeerId;

    // Noise public key (Curve25519, derived from Ed25519 identity)
    uint8_t _noisePublicKey[32];

    // Default channel for Dogechat messages
    char _defaultChannelName[32];
    mesh::GroupChannel _meshcoreChannel;
    bool _channelConfigured;

    // Channel registry for bidirectional mapping
    struct ChannelMapping {
        char dogechatName[32];      // Channel name without # prefix
        mesh::GroupChannel meshChannel;
        bool configured;
    };
    static const size_t MAX_CHANNEL_MAPPINGS = 4;
    ChannelMapping _channelMappings[MAX_CHANNEL_MAPPINGS];

    // Announcement timing
    uint32_t _lastAnnounceTime;
    volatile bool _pendingAnnounce;  // Flag to defer announcement to main loop (BLE callback has limited stack)
    static const uint32_t ANNOUNCE_INTERVAL_MS = 5000;  // 5 seconds when idle
    static const uint32_t ANNOUNCE_INTERVAL_CONNECTED_MS = 3000;  // 3 seconds when client connected

    // Time synchronization (calibrated from received Dogechat packets)
    // Android sends Unix timestamps; we sync from them since ESP32 may not have valid RTC
    int64_t _timeOffset;      // Offset to add to millis() to get Unix time (ms)
    bool _timeSynced;         // True after receiving at least one valid timestamp from Android

    // Statistics
    uint32_t _messagesRelayed;
    uint32_t _duplicatesDropped;

    // TTL for outgoing messages
    static const uint8_t DEFAULT_TTL = 8;

    // #mesh channel configuration
    mesh::GroupChannel _meshChannel;        // The MeshCore #mesh channel
    bool _meshChannelConfigured;            // True if #mesh channel is found/configured
    int _meshChannelIndex;                  // Index in the mesh's channel array (-1 if not found)

    // Message history cache for REQUEST_SYNC
    // Stores recent messages so we can respond to sync requests
    struct CachedMessage {
        DogechatMessage msg;
        uint32_t addedTimeMs;  // millis() when message was cached (for expiration)
        bool valid;
    };
    static const size_t MESSAGE_HISTORY_SIZE = 16;
    static const uint32_t MESSAGE_EXPIRY_MS = 300000;  // 5 minutes
    CachedMessage _messageHistory[MESSAGE_HISTORY_SIZE];
    size_t _messageHistoryHead;

    /**
     * Golomb-Coded Set (GCS) filter for REQUEST_SYNC
     * Used to determine which messages the requester already has.
     * See Android RequestSyncPacket.kt for format details.
     */
    struct GCSFilter {
        uint8_t p;           // Golomb-Rice parameter (bits for remainder)
        uint32_t n;          // Number of elements in filter
        uint32_t m;          // Range M = N * 2^P
        const uint8_t* data; // Pointer to encoded bitstream
        size_t dataLen;      // Length of encoded data

        /**
         * Check if a packet ID might be in the filter (probabilistic)
         * @param packetId16 16-byte packet ID
         * @return true if the ID might be in the filter (requester may have it)
         */
        bool mightContain(const uint8_t* packetId16) const;
    };

    /**
     * Parse GCS filter from REQUEST_SYNC payload
     * @param payload REQUEST_SYNC payload (TLV encoded)
     * @param len Payload length
     * @param outFilter Output filter structure
     * @return true if filter was successfully parsed
     */
    bool parseGCSFilter(const uint8_t* payload, size_t len, GCSFilter& outFilter);

    /**
     * Find or configure the #mesh channel
     * Called during begin() to set up the channel for relaying
     */
    void configureMeshChannel();

    /**
     * Check if a MeshCore channel matches the #mesh channel by key
     * @param channel Channel to check
     * @return true if it's the #mesh channel
     */
    bool isMeshChannel(const mesh::GroupChannel& channel) const;

    /**
     * Add a message to the history cache
     * @param msg Message to cache
     */
    void addToMessageHistory(const DogechatMessage& msg);

    /**
     * Handle REQUEST_SYNC by sending cached messages
     * @param msg The REQUEST_SYNC message
     */
    void handleRequestSync(const DogechatMessage& msg);

    // Peer nickname cache (populated from ANNOUNCE messages)
    struct PeerInfo {
        uint64_t peerId;
        char nickname[16];    // 13 chars + null + padding
        uint32_t timestamp;   // millis() when last seen
        bool valid;
    };
    static const size_t PEER_CACHE_SIZE = 32;
    PeerInfo _peerCache[PEER_CACHE_SIZE];

    // Fragment reassembly buffers for long messages
    // Dogechat fragments messages >245 bytes into multiple FRAGMENT messages
    struct FragmentBuffer {
        uint64_t senderId;
        uint8_t fragmentId;
        uint8_t totalFragments;
        uint8_t receivedMask;  // Bitmask of received fragments (up to 8 fragments)
        uint8_t data[2048];    // Reassembly buffer
        size_t dataLen;
        uint32_t startTime;    // millis() when first fragment received
        bool active;
    };
    static const size_t MAX_FRAGMENT_BUFFERS = 4;
    static const uint32_t FRAGMENT_TIMEOUT_MS = 10000;  // 10 second timeout
    FragmentBuffer _fragmentBuffers[MAX_FRAGMENT_BUFFERS];

    /**
     * Handle incoming fragment message
     * Reassembles multi-fragment messages and processes when complete
     */
    void handleFragment(const DogechatMessage& msg);

    /**
     * Derive Dogechat peer ID from Meshcore identity
     * Uses first 8 bytes of public key
     */
    uint64_t derivePeerId(const mesh::LocalIdentity& identity);

    /**
     * Derive Noise public key (Curve25519) from Ed25519 public key
     * Uses standard Edwards→Montgomery conversion
     */
    void deriveNoisePublicKey(const uint8_t* ed25519PubKey, uint8_t* curve25519PubKey);

    /**
     * Send peer announcement to connected Dogechat clients
     */
    void sendPeerAnnouncement();

    /**
     * Sign a Dogechat message with the companion's Ed25519 identity
     * Uses Dogechat protocol signing rules (TTL=0, PKCS#7 padding)
     */
    void signMessage(DogechatMessage& msg);

    /**
     * Process incoming Dogechat message from BLE
     */
    void processDogechatMessage(const DogechatMessage& msg);

    /**
     * Relay Dogechat channel message to Meshcore mesh
     * @param msg Original Dogechat message
     * @param channelName Channel name (e.g., "#mesh")
     * @param senderNick Sender's nickname from Dogechat
     * @param text Message content
     */
    void relayChannelMessageToMesh(const DogechatMessage& msg, const char* channelName,
                                   const char* senderNick, const char* text);

    /**
     * Relay Dogechat DM to Meshcore mesh
     */
    void relayDirectMessageToMesh(const DogechatMessage& msg, const char* text);

    /**
     * Broadcast Dogechat message to connected BLE clients
     */
    void broadcastToDogechat(const DogechatMessage& msg);

    /**
     * Get current time in milliseconds (for Dogechat timestamps)
     * Returns synchronized time if available, otherwise falls back to RTC or millis()
     */
    uint64_t getCurrentTimeMs();

    /**
     * Synchronize local time from a received Dogechat packet timestamp
     * This is critical for proper operation - Android rejects announces with stale timestamps
     */
    void syncTimeFromPacket(uint64_t packetTimestamp);

    /**
     * Parse ANNOUNCE message TLV to extract nickname
     * @param payload TLV-encoded payload
     * @param len Payload length
     * @param nickname OUT: Nickname if found
     * @param nickLen Capacity of nickname buffer
     * @return true if nickname was found
     */
    bool parseAnnounceTLV(const uint8_t* payload, size_t len, char* nickname, size_t nickLen);

    /**
     * Cache or update a peer's nickname
     * @param peerId Peer's 64-bit ID
     * @param nickname Peer's nickname
     */
    void cachePeer(uint64_t peerId, const char* nickname);

    /**
     * Look up a cached peer nickname
     * @param peerId Peer's 64-bit ID
     * @return Nickname or nullptr if not found
     */
    const char* lookupPeerNickname(uint64_t peerId);

    /**
     * Send a single message part to the mesh (immediate, no delay)
     * @param senderNick Sender nickname with emoji prefix
     * @param text Message text (may include part indicator)
     */
    void sendSingleMessageToMesh(const char* senderNick, const char* text);

    // Pending message parts queue for reliable multi-part message delivery
    // Instead of using mesh's delayed transmission (which can fail silently when pool is exhausted),
    // we queue parts here and send them one at a time with timer-based delays in loop()
    struct PendingPart {
        char senderNick[68];    // Includes emoji prefix
        char text[180];         // Part text with "[X/Y] " indicator
        bool valid;
    };
    static const size_t MAX_PENDING_PARTS = 8;
    static const uint32_t PART_SEND_DELAY_MS = 5000;  // Delay between parts
    PendingPart _pendingParts[MAX_PENDING_PARTS];
    size_t _pendingPartsHead;        // Next part to send
    size_t _pendingPartsTail;        // Next slot to queue into
    uint32_t _lastPartSentTime;      // millis() when last part was sent

    /**
     * Queue a message part for delayed sending
     * @param senderNick Sender nickname with emoji prefix
     * @param text Message text (may include part indicator)
     * @return true if queued successfully
     */
    bool queueMessagePart(const char* senderNick, const char* text);

    /**
     * Process pending message parts queue (called from loop())
     */
    void processPendingParts();

    /**
     * Parse DogechatMessage TLV payload structure
     *
     * The payload contains:
     * [flags:1][timestamp:8][idLen:1][id:N][senderLen:1][sender:N]
     * [contentLen:2][content:N]...[channelLen:1][channel:N if hasChannel]
     *
     * @param payload Message payload (TLV encoded)
     * @param payloadLen Payload length
     * @param senderNick OUT: Sender's nickname
     * @param senderNickLen Capacity of senderNick buffer
     * @param content OUT: Message content (text)
     * @param contentLen Capacity of content buffer
     * @param channelName OUT: Channel name (with #), empty if DM
     * @param channelNameLen Capacity of channelName buffer
     * @return true if successfully parsed
     */
    bool parseDogechatMessageTLV(const uint8_t* payload, size_t payloadLen,
                                char* senderNick, size_t senderNickLen,
                                char* content, size_t contentLen,
                                char* channelName, size_t channelNameLen);
};
