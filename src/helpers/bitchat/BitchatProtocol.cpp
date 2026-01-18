#include "DogechatProtocol.h"
#include "../../Utils.h"

// Platform-specific miniz includes for DEFLATE decompression
#if defined(ESP32)
  #include <Arduino.h>  // For Serial debug output
  // Use ESP-IDF's ROM miniz for raw deflate decompression
  // tinfl_decompress_mem_to_mem() is available in ESP32 ROM
  extern "C" {
  #include "rom/miniz.h"
  }
  #define DOGECHAT_HAS_DECOMPRESSION 1
#elif defined(NRF52_PLATFORM)
  // Use portable miniz library for NRF52
  #include "../miniz/miniz_tinfl.h"
  #define DOGECHAT_HAS_DECOMPRESSION 1
#else
  // No decompression support on other platforms
  #define DOGECHAT_HAS_DECOMPRESSION 0
#endif

// ============================================================================
// DogechatDuplicateCache
// ============================================================================

DogechatDuplicateCache::DogechatDuplicateCache() : currentIndex(0) {
    clear();
}

void DogechatDuplicateCache::clear() {
    for (size_t i = 0; i < DOGECHAT_DUPLICATE_CACHE_SIZE; i++) {
        cache[i].valid = false;
        cache[i].hash = 0;
        cache[i].timestamp = 0;
    }
    currentIndex = 0;
}

uint32_t DogechatDuplicateCache::calculateHash(const DogechatMessage& msg) const {
    // FNV-1a hash algorithm
    uint32_t hash = 2166136261u;  // FNV offset basis

    // Hash sender ID
    for (int i = 0; i < DOGECHAT_SENDER_ID_SIZE; i++) {
        hash ^= msg.senderId[i];
        hash *= 16777619u;  // FNV prime
    }

    // Hash timestamp (lower 32 bits, in seconds for tolerance)
    uint32_t ts_sec = static_cast<uint32_t>(msg.timestamp / 1000);
    hash ^= (ts_sec & 0xFF);
    hash *= 16777619u;
    hash ^= ((ts_sec >> 8) & 0xFF);
    hash *= 16777619u;
    hash ^= ((ts_sec >> 16) & 0xFF);
    hash *= 16777619u;
    hash ^= ((ts_sec >> 24) & 0xFF);
    hash *= 16777619u;

    // Hash message type
    hash ^= msg.type;
    hash *= 16777619u;

    // Hash payload length
    hash ^= (msg.payloadLength & 0xFF);
    hash *= 16777619u;
    hash ^= ((msg.payloadLength >> 8) & 0xFF);
    hash *= 16777619u;

    // Hash first 16 bytes of payload (if available)
    size_t hashLen = msg.payloadLength < 16 ? msg.payloadLength : 16;
    for (size_t i = 0; i < hashLen; i++) {
        hash ^= msg.payload[i];
        hash *= 16777619u;
    }

    return hash;
}

bool DogechatDuplicateCache::isDuplicate(const DogechatMessage& msg) {
    uint32_t hash = calculateHash(msg);
    uint32_t ts_sec = static_cast<uint32_t>(msg.timestamp / 1000);

    // Check existing entries
    for (size_t i = 0; i < DOGECHAT_DUPLICATE_CACHE_SIZE; i++) {
        if (!cache[i].valid) continue;

        if (cache[i].hash == hash) {
            // Allow ±5 second timestamp tolerance for duplicates
            int32_t timeDiff = static_cast<int32_t>(ts_sec) - static_cast<int32_t>(cache[i].timestamp);
            if (timeDiff >= -5 && timeDiff <= 5) {
                return true;
            }
        }
    }

    // Not a duplicate - add to cache
    addMessage(msg);
    return false;
}

void DogechatDuplicateCache::addMessage(const DogechatMessage& msg) {
    cache[currentIndex].hash = calculateHash(msg);
    cache[currentIndex].timestamp = static_cast<uint32_t>(msg.timestamp / 1000);
    cache[currentIndex].valid = true;

    currentIndex = (currentIndex + 1) % DOGECHAT_DUPLICATE_CACHE_SIZE;
}

// ============================================================================
// DogechatProtocol - Helper functions
// ============================================================================

uint16_t DogechatProtocol::readBE16(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint64_t DogechatProtocol::readBE64(const uint8_t* data) {
    uint64_t result = 0;
    for (int i = 0; i < 8; i++) {
        result = (result << 8) | data[i];
    }
    return result;
}

void DogechatProtocol::writeBE16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(value & 0xFF);
}

void DogechatProtocol::writeBE64(uint8_t* data, uint64_t value) {
    for (int i = 7; i >= 0; i--) {
        data[7 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
}

// ============================================================================
// DogechatProtocol - Parsing and Serialization
// ============================================================================

bool DogechatProtocol::parseMessage(const uint8_t* data, size_t length, DogechatMessage& msg) {
    if (length < DOGECHAT_HEADER_SIZE) {
        return false;
    }

    size_t offset = 0;

    // Parse header
    msg.version = data[offset++];
    msg.type = data[offset++];
    msg.ttl = data[offset++];
    msg.timestamp = readBE64(&data[offset]);
    offset += 8;
    msg.flags = data[offset++];
    msg.payloadLength = readBE16(&data[offset]);
    offset += 2;

    // Validate version
    if (msg.version != DOGECHAT_VERSION) {
        return false;
    }

    // Validate payload length
    if (msg.payloadLength > DOGECHAT_MAX_PAYLOAD_SIZE) {
        return false;
    }

    // Calculate expected message size
    size_t expectedSize = DOGECHAT_HEADER_SIZE + DOGECHAT_SENDER_ID_SIZE;
    if (msg.hasRecipient()) {
        expectedSize += DOGECHAT_RECIPIENT_ID_SIZE;
    }
    expectedSize += msg.payloadLength;
    if (msg.hasSignature()) {
        expectedSize += DOGECHAT_SIGNATURE_SIZE;
    }

    if (length < expectedSize) {
        return false;
    }

    // Parse sender ID
    memcpy(msg.senderId, &data[offset], DOGECHAT_SENDER_ID_SIZE);
    offset += DOGECHAT_SENDER_ID_SIZE;

    // Parse recipient ID (if present)
    memset(msg.recipientId, 0, DOGECHAT_RECIPIENT_ID_SIZE);
    if (msg.hasRecipient()) {
        memcpy(msg.recipientId, &data[offset], DOGECHAT_RECIPIENT_ID_SIZE);
        offset += DOGECHAT_RECIPIENT_ID_SIZE;
    }

    // Parse payload
    memset(msg.payload, 0, DOGECHAT_MAX_PAYLOAD_SIZE);
    uint16_t wirePayloadLength = msg.payloadLength;  // Save original wire length
    if (wirePayloadLength > 0) {
        // Check if payload is compressed
        if (msg.isCompressed()) {
#if DOGECHAT_HAS_DECOMPRESSION
            // Compressed payload format (from Android CompressionUtil.kt):
            // - First 2 bytes: original uncompressed size (big-endian)
            // - Remaining bytes: raw deflate compressed data
            if (wirePayloadLength < 3) {
                return false;
            }

            // Read original size from first 2 bytes
            uint16_t originalSize = (data[offset] << 8) | data[offset + 1];
            const uint8_t* compressedData = &data[offset + 2];
            size_t compressedLen = wirePayloadLength - 2;

            if (originalSize > DOGECHAT_MAX_PAYLOAD_SIZE) {
                return false;
            }

            // Use streaming tinfl API with heap-allocated decompressor to avoid stack overflow
            // tinfl_decompress_mem_to_mem allocates ~10KB decompressor internally on stack
            tinfl_decompressor* decomp = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
            uint8_t* decompBuffer = (uint8_t*)malloc(DOGECHAT_MAX_PAYLOAD_SIZE);

            if (decomp == nullptr || decompBuffer == nullptr) {
                if (decomp) free(decomp);
                if (decompBuffer) free(decompBuffer);
                return false;
            }

            tinfl_init(decomp);

            size_t inBytes = compressedLen;
            size_t outBytes = DOGECHAT_MAX_PAYLOAD_SIZE;

            // TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF is required for linear output buffer
            // Without it, tinfl expects a ring buffer dictionary
            const int linearBufFlag = TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;

            // Try raw deflate first (Android uses raw deflate, not zlib)
            tinfl_status status = tinfl_decompress(
                decomp,
                compressedData,         // Input
                &inBytes,               // Input size (updated)
                decompBuffer,           // Output buffer start
                decompBuffer,           // Output write position
                &outBytes,              // Output size (updated)
                linearBufFlag           // Linear buffer, raw deflate
            );

            // If raw deflate failed, try with zlib header
            if (status != TINFL_STATUS_DONE) {
                tinfl_init(decomp);
                inBytes = compressedLen;
                outBytes = DOGECHAT_MAX_PAYLOAD_SIZE;
                status = tinfl_decompress(
                    decomp,
                    compressedData,
                    &inBytes,
                    decompBuffer,
                    decompBuffer,
                    &outBytes,
                    linearBufFlag | TINFL_FLAG_PARSE_ZLIB_HEADER
                );
            }

            if (status != TINFL_STATUS_DONE) {
                free(decomp);
                free(decompBuffer);
                return false;
            }

            // Copy decompressed data to message payload
            size_t decompressedLen = outBytes;
            memcpy(msg.payload, decompBuffer, decompressedLen);
            free(decomp);
            free(decompBuffer);

            msg.payloadLength = static_cast<uint16_t>(decompressedLen);
            msg.flags &= ~DOGECHAT_FLAG_IS_COMPRESSED;  // Clear compressed flag
#else
            // Platforms without decompression support: just copy raw payload
            memcpy(msg.payload, &data[offset], wirePayloadLength);
#endif
        } else {
            // Uncompressed payload - direct copy
            memcpy(msg.payload, &data[offset], wirePayloadLength);
        }
        offset += wirePayloadLength;
    }

    // Parse signature (if present)
    memset(msg.signature, 0, DOGECHAT_SIGNATURE_SIZE);
    if (msg.hasSignature()) {
        memcpy(msg.signature, &data[offset], DOGECHAT_SIGNATURE_SIZE);
        offset += DOGECHAT_SIGNATURE_SIZE;
    }

    return true;
}

size_t DogechatProtocol::serializeMessage(const DogechatMessage& msg, uint8_t* buffer, size_t maxLength) {
    size_t requiredSize = getMessageSize(msg);
    if (maxLength < requiredSize) {
        return 0;
    }

    size_t offset = 0;

    // Write header
    buffer[offset++] = msg.version;
    buffer[offset++] = msg.type;
    buffer[offset++] = msg.ttl;
    writeBE64(&buffer[offset], msg.timestamp);
    offset += 8;
    buffer[offset++] = msg.flags;
    writeBE16(&buffer[offset], msg.payloadLength);
    offset += 2;

    // Write sender ID
    memcpy(&buffer[offset], msg.senderId, DOGECHAT_SENDER_ID_SIZE);
    offset += DOGECHAT_SENDER_ID_SIZE;

    // Write recipient ID (if present)
    if (msg.hasRecipient()) {
        memcpy(&buffer[offset], msg.recipientId, DOGECHAT_RECIPIENT_ID_SIZE);
        offset += DOGECHAT_RECIPIENT_ID_SIZE;
    }

    // Write payload
    if (msg.payloadLength > 0) {
        memcpy(&buffer[offset], msg.payload, msg.payloadLength);
        offset += msg.payloadLength;
    }

    // Write signature (if present)
    if (msg.hasSignature()) {
        memcpy(&buffer[offset], msg.signature, DOGECHAT_SIGNATURE_SIZE);
        offset += DOGECHAT_SIGNATURE_SIZE;
    }

    return offset;
}

bool DogechatProtocol::validateMessage(const DogechatMessage& msg) {
    // Check version
    if (msg.version != DOGECHAT_VERSION) {
        return false;
    }

    // Check type is valid
    switch (msg.type) {
        case DOGECHAT_MSG_ANNOUNCE:
        case DOGECHAT_MSG_MESSAGE:
        case DOGECHAT_MSG_LEAVE:
        case DOGECHAT_MSG_IDENTITY:
        case DOGECHAT_MSG_CHANNEL:
        case DOGECHAT_MSG_PING:
        case DOGECHAT_MSG_PONG:
        case DOGECHAT_MSG_NOISE_HANDSHAKE:
        case DOGECHAT_MSG_NOISE_ENCRYPTED:
        case DOGECHAT_MSG_FRAGMENT_NEW:
        case DOGECHAT_MSG_REQUEST_SYNC:
        case DOGECHAT_MSG_FILE_TRANSFER:
        case DOGECHAT_MSG_FRAGMENT:
            break;
        default:
            return false;
    }

    // Check payload length
    if (msg.payloadLength > DOGECHAT_MAX_PAYLOAD_SIZE) {
        return false;
    }

    // Check sender ID is non-zero
    bool senderNonZero = false;
    for (int i = 0; i < DOGECHAT_SENDER_ID_SIZE; i++) {
        if (msg.senderId[i] != 0) {
            senderNonZero = true;
            break;
        }
    }
    if (!senderNonZero) {
        return false;
    }

    return true;
}

size_t DogechatProtocol::getMessageSize(const DogechatMessage& msg) {
    size_t size = DOGECHAT_HEADER_SIZE + DOGECHAT_SENDER_ID_SIZE;

    if (msg.hasRecipient()) {
        size += DOGECHAT_RECIPIENT_ID_SIZE;
    }

    size += msg.payloadLength;

    if (msg.hasSignature()) {
        size += DOGECHAT_SIGNATURE_SIZE;
    }

    return size;
}

void DogechatProtocol::computePacketId(const DogechatMessage& msg, uint8_t* outId16) {
    // Compute packet ID matching Android Dogechat:
    // SHA-256(type | senderId | timestamp_BE | payload)[0:16]
    //
    // This creates a deterministic unique ID for each message based on its content.
    // Used by GCS filter to detect which messages the requester already has.

    // Build the data to hash: type(1) + senderId(8) + timestamp(8 BE) + payload
    uint8_t hashInput[1 + DOGECHAT_SENDER_ID_SIZE + 8 + DOGECHAT_MAX_PAYLOAD_SIZE];
    size_t offset = 0;

    // Type (1 byte)
    hashInput[offset++] = msg.type;

    // Sender ID (8 bytes, as stored - little endian)
    memcpy(&hashInput[offset], msg.senderId, DOGECHAT_SENDER_ID_SIZE);
    offset += DOGECHAT_SENDER_ID_SIZE;

    // Timestamp (8 bytes, big-endian)
    writeBE64(&hashInput[offset], msg.timestamp);
    offset += 8;

    // Payload
    if (msg.payloadLength > 0) {
        memcpy(&hashInput[offset], msg.payload, msg.payloadLength);
        offset += msg.payloadLength;
    }

    // Compute SHA-256 and truncate to 16 bytes
    uint8_t fullHash[32];
    mesh::Utils::sha256(fullHash, 32, hashInput, static_cast<int>(offset));

    // Copy first 16 bytes as the packet ID
    memcpy(outId16, fullHash, 16);
}

// ============================================================================
// DogechatProtocol - Message Creation
// ============================================================================

void DogechatProtocol::createAnnounce(DogechatMessage& msg, uint64_t senderId, const char* nickname,
                                     const uint8_t* noisePublicKey, const uint8_t* signingPublicKey,
                                     uint64_t timestamp, uint8_t ttl) {
    msg.version = DOGECHAT_VERSION;
    msg.type = DOGECHAT_MSG_ANNOUNCE;
    msg.ttl = ttl;
    msg.timestamp = timestamp;
    msg.flags = 0;  // No recipient, no signature for basic announce
    msg.setSenderId64(senderId);

    // Build TLV payload
    size_t offset = 0;

    // Add nickname TLV (0x01)
    // NOTE: Nickname is limited to 13 bytes to ensure signed announce packet fits within
    // BLE MTU of 169 bytes. Total: header(13) + sender(8) + payload(84 max) + sig(64) = 169
    // Payload: nick_tlv(2+13=15) + noise_tlv(34) + ed25519_tlv(34) = 83 bytes
    if (nickname != nullptr && nickname[0] != '\0') {
        size_t nickLen = strlen(nickname);
        if (nickLen > 13) nickLen = 13;  // Limit to ensure packet fits in 169-byte MTU

        if (offset + 2 + nickLen <= DOGECHAT_MAX_PAYLOAD_SIZE) {
            msg.payload[offset++] = DOGECHAT_TLV_NICKNAME;
            msg.payload[offset++] = static_cast<uint8_t>(nickLen);
            memcpy(&msg.payload[offset], nickname, nickLen);
            offset += nickLen;
        }
    }

    // Add Noise public key TLV (0x02) - Curve25519 for Noise protocol
    if (noisePublicKey != nullptr) {
        if (offset + 2 + 32 <= DOGECHAT_MAX_PAYLOAD_SIZE) {
            msg.payload[offset++] = DOGECHAT_TLV_NOISE_PUBKEY;
            msg.payload[offset++] = 32;
            memcpy(&msg.payload[offset], noisePublicKey, 32);
            offset += 32;
        }
    }

    // Add Ed25519 signing public key TLV (0x03)
    if (signingPublicKey != nullptr) {
        if (offset + 2 + 32 <= DOGECHAT_MAX_PAYLOAD_SIZE) {
            msg.payload[offset++] = DOGECHAT_TLV_ED25519_PUBKEY;
            msg.payload[offset++] = 32;
            memcpy(&msg.payload[offset], signingPublicKey, 32);
            offset += 32;
        }
    }

    msg.payloadLength = static_cast<uint16_t>(offset);
}

void DogechatProtocol::createTextMessage(DogechatMessage& msg, uint64_t senderId, uint64_t recipientId,
                                        const char* channelName, const char* text, size_t textLen,
                                        uint64_t timestamp, uint8_t ttl) {
    msg.version = DOGECHAT_VERSION;
    msg.type = DOGECHAT_MSG_MESSAGE;
    msg.ttl = ttl;
    msg.timestamp = timestamp;
    msg.setSenderId64(senderId);

    size_t offset = 0;

    if (recipientId != 0) {
        // Direct message
        msg.flags = DOGECHAT_FLAG_HAS_RECIPIENT;
        msg.setRecipientId64(recipientId);

        // Payload is just the text
        if (textLen > DOGECHAT_MAX_PAYLOAD_SIZE) {
            textLen = DOGECHAT_MAX_PAYLOAD_SIZE;
        }
        memcpy(msg.payload, text, textLen);
        offset = textLen;
    } else if (channelName != nullptr && channelName[0] != '\0') {
        // Channel message - format: "#channel:text"
        msg.flags = 0;  // No recipient
        memset(msg.recipientId, 0, DOGECHAT_RECIPIENT_ID_SIZE);

        // Build payload: #channelname\0text
        size_t channelLen = strlen(channelName);

        // Add # prefix and channel name
        if (offset < DOGECHAT_MAX_PAYLOAD_SIZE) {
            msg.payload[offset++] = '#';
        }
        for (size_t i = 0; i < channelLen && offset < DOGECHAT_MAX_PAYLOAD_SIZE; i++) {
            msg.payload[offset++] = channelName[i];
        }

        // Add separator (colon)
        if (offset < DOGECHAT_MAX_PAYLOAD_SIZE) {
            msg.payload[offset++] = ':';
        }

        // Add text
        for (size_t i = 0; i < textLen && offset < DOGECHAT_MAX_PAYLOAD_SIZE; i++) {
            msg.payload[offset++] = text[i];
        }
    } else {
        // No recipient and no channel - invalid, but handle gracefully
        msg.flags = 0;
        memset(msg.recipientId, 0, DOGECHAT_RECIPIENT_ID_SIZE);
        if (textLen > DOGECHAT_MAX_PAYLOAD_SIZE) {
            textLen = DOGECHAT_MAX_PAYLOAD_SIZE;
        }
        memcpy(msg.payload, text, textLen);
        offset = textLen;
    }

    msg.payloadLength = static_cast<uint16_t>(offset);
}
