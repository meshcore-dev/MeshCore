#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Dogechat Protocol Constants
#define DOGECHAT_HEADER_SIZE 14  // version(1) + type(1) + ttl(1) + timestamp(8) + flags(1) + payloadLength(2)
#define DOGECHAT_SIGNATURE_SIZE 64  // Ed25519 signature
#define DOGECHAT_MAX_WIRE_PAYLOAD_SIZE 245  // Max payload size on wire (compressed/padded)
#define DOGECHAT_MAX_PAYLOAD_SIZE 512   // Max decompressed payload size (reduced from 1024 to prevent stack overflow)
#define DOGECHAT_VERSION 1
#define DOGECHAT_SENDER_ID_SIZE 8
#define DOGECHAT_RECIPIENT_ID_SIZE 8

// Maximum message size on wire: header(13) + sender(8) + recipient(8) + payload(245) + signature(64) = 338 bytes
#define DOGECHAT_MAX_MESSAGE_SIZE (DOGECHAT_HEADER_SIZE + DOGECHAT_SENDER_ID_SIZE + DOGECHAT_RECIPIENT_ID_SIZE + DOGECHAT_MAX_WIRE_PAYLOAD_SIZE + DOGECHAT_SIGNATURE_SIZE)

// BLE Service UUIDs
#define DOGECHAT_SERVICE_UUID "F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C"
#define DOGECHAT_CHARACTERISTIC_UUID "A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D"

// Duplicate cache configuration
#define DOGECHAT_DUPLICATE_CACHE_SIZE 100
#define DOGECHAT_DUPLICATE_TIME_WINDOW_MS 300000  // 5 minutes

// Dogechat Message Types
enum DogechatMessageType : uint8_t {
    DOGECHAT_MSG_ANNOUNCE = 0x01,
    DOGECHAT_MSG_MESSAGE = 0x02,
    DOGECHAT_MSG_LEAVE = 0x03,
    DOGECHAT_MSG_IDENTITY = 0x04,
    DOGECHAT_MSG_CHANNEL = 0x05,
    DOGECHAT_MSG_PING = 0x06,
    DOGECHAT_MSG_PONG = 0x07,
    DOGECHAT_MSG_NOISE_HANDSHAKE = 0x10,
    DOGECHAT_MSG_NOISE_ENCRYPTED = 0x11,
    DOGECHAT_MSG_FRAGMENT_NEW = 0x20,
    DOGECHAT_MSG_REQUEST_SYNC = 0x21,
    DOGECHAT_MSG_FILE_TRANSFER = 0x22,
    DOGECHAT_MSG_FRAGMENT = 0xFF
};

// Dogechat Protocol Flags
#define DOGECHAT_FLAG_HAS_RECIPIENT 0x01
#define DOGECHAT_FLAG_HAS_SIGNATURE 0x02
#define DOGECHAT_FLAG_IS_COMPRESSED 0x04

// Announce payload TLV types
#define DOGECHAT_TLV_NICKNAME 0x01
#define DOGECHAT_TLV_NOISE_PUBKEY 0x02
#define DOGECHAT_TLV_ED25519_PUBKEY 0x03

/**
 * Dogechat Protocol Message Structure
 * Matches iOS BinaryProtocol format for compatibility
 */
struct DogechatMessage {
    uint8_t version;
    uint8_t type;
    uint8_t ttl;
    uint64_t timestamp;  // milliseconds since epoch
    uint8_t flags;
    uint16_t payloadLength;
    uint8_t senderId[DOGECHAT_SENDER_ID_SIZE];
    uint8_t recipientId[DOGECHAT_RECIPIENT_ID_SIZE];
    uint8_t payload[DOGECHAT_MAX_PAYLOAD_SIZE];
    uint8_t signature[DOGECHAT_SIGNATURE_SIZE];

    DogechatMessage() : version(DOGECHAT_VERSION), type(0), ttl(0), timestamp(0), flags(0), payloadLength(0) {
        memset(senderId, 0, sizeof(senderId));
        memset(recipientId, 0, sizeof(recipientId));
        memset(payload, 0, sizeof(payload));
        memset(signature, 0, sizeof(signature));
    }

    bool hasRecipient() const { return (flags & DOGECHAT_FLAG_HAS_RECIPIENT) != 0; }
    bool hasSignature() const { return (flags & DOGECHAT_FLAG_HAS_SIGNATURE) != 0; }
    bool isCompressed() const { return (flags & DOGECHAT_FLAG_IS_COMPRESSED) != 0; }

    void setHasRecipient(bool val) { if (val) flags |= DOGECHAT_FLAG_HAS_RECIPIENT; else flags &= ~DOGECHAT_FLAG_HAS_RECIPIENT; }
    void setHasSignature(bool val) { if (val) flags |= DOGECHAT_FLAG_HAS_SIGNATURE; else flags &= ~DOGECHAT_FLAG_HAS_SIGNATURE; }

    // Get sender ID as 64-bit integer (little-endian)
    uint64_t getSenderId64() const {
        uint64_t id = 0;
        for (int i = 0; i < 8; i++) {
            id |= (static_cast<uint64_t>(senderId[i]) << (i * 8));
        }
        return id;
    }

    // Set sender ID from 64-bit integer (little-endian)
    void setSenderId64(uint64_t id) {
        for (int i = 0; i < 8; i++) {
            senderId[i] = static_cast<uint8_t>((id >> (i * 8)) & 0xFF);
        }
    }

    // Get recipient ID as 64-bit integer
    uint64_t getRecipientId64() const {
        uint64_t id = 0;
        for (int i = 0; i < 8; i++) {
            id |= (static_cast<uint64_t>(recipientId[i]) << (i * 8));
        }
        return id;
    }

    // Set recipient ID from 64-bit integer
    void setRecipientId64(uint64_t id) {
        for (int i = 0; i < 8; i++) {
            recipientId[i] = static_cast<uint8_t>((id >> (i * 8)) & 0xFF);
        }
    }
};

/**
 * Duplicate Message Cache
 * Prevents message loops by tracking recently seen messages
 */
class DogechatDuplicateCache {
public:
    DogechatDuplicateCache();

    // Check if message is a duplicate (and add if not)
    bool isDuplicate(const DogechatMessage& msg);

    // Explicitly add a message to the cache
    void addMessage(const DogechatMessage& msg);

    // Clear the cache
    void clear();

private:
    struct CacheEntry {
        uint32_t hash;
        uint32_t timestamp;  // seconds
        bool valid;

        CacheEntry() : hash(0), timestamp(0), valid(false) {}
    };

    CacheEntry cache[DOGECHAT_DUPLICATE_CACHE_SIZE];
    size_t currentIndex;

    // FNV-1a hash
    uint32_t calculateHash(const DogechatMessage& msg) const;
};

/**
 * Protocol parsing and serialization
 */
class DogechatProtocol {
public:
    /**
     * Parse a binary buffer into a DogechatMessage
     * @param data Input buffer
     * @param length Buffer length
     * @param msg Output message
     * @return true if parsing succeeded
     */
    static bool parseMessage(const uint8_t* data, size_t length, DogechatMessage& msg);

    /**
     * Serialize a DogechatMessage to a binary buffer
     * @param msg Input message
     * @param buffer Output buffer
     * @param maxLength Buffer capacity
     * @return Number of bytes written, or 0 on failure
     */
    static size_t serializeMessage(const DogechatMessage& msg, uint8_t* buffer, size_t maxLength);

    /**
     * Validate a DogechatMessage
     * @param msg Message to validate
     * @return true if message is valid
     */
    static bool validateMessage(const DogechatMessage& msg);

    /**
     * Get the serialized size of a message
     * @param msg Message to measure
     * @return Size in bytes
     */
    static size_t getMessageSize(const DogechatMessage& msg);

    /**
     * Compute the deterministic packet ID for a message
     * This matches the Android Dogechat packet ID computation:
     * SHA-256(type | senderId | timestamp_BE | payload)[0:16]
     *
     * @param msg Message to compute ID for
     * @param outId16 Output buffer (16 bytes) for the packet ID
     */
    static void computePacketId(const DogechatMessage& msg, uint8_t* outId16);

    /**
     * Create an ANNOUNCE message
     * @param msg Output message
     * @param senderId Sender's 64-bit ID
     * @param nickname Sender's nickname (null-terminated)
     * @param noisePublicKey Curve25519 public key for Noise protocol (32 bytes, can be NULL)
     * @param signingPublicKey Ed25519 public key for signatures (32 bytes, can be NULL)
     * @param timestamp Current time in milliseconds
     * @param ttl Time-to-live
     */
    static void createAnnounce(DogechatMessage& msg, uint64_t senderId, const char* nickname,
                               const uint8_t* noisePublicKey, const uint8_t* signingPublicKey,
                               uint64_t timestamp, uint8_t ttl);

    /**
     * Create a MESSAGE (text message)
     * @param msg Output message
     * @param senderId Sender's 64-bit ID
     * @param recipientId Recipient's 64-bit ID (0 for channel message)
     * @param channelName Channel name (for channel messages, without #)
     * @param text Message text
     * @param textLen Text length
     * @param timestamp Current time in milliseconds
     * @param ttl Time-to-live
     */
    static void createTextMessage(DogechatMessage& msg, uint64_t senderId, uint64_t recipientId,
                                  const char* channelName, const char* text, size_t textLen,
                                  uint64_t timestamp, uint8_t ttl);

private:
    // Read big-endian uint16
    static uint16_t readBE16(const uint8_t* data);

    // Read big-endian uint64
    static uint64_t readBE64(const uint8_t* data);

    // Write big-endian uint16
    static void writeBE16(uint8_t* data, uint16_t value);

    // Write big-endian uint64
    static void writeBE64(uint8_t* data, uint64_t value);
};
