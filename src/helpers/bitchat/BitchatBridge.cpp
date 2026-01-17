#include "BitchatBridge.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

// Include ed25519 field element operations for Ed25519→Curve25519 conversion
extern "C" {
#include "fe.h"
}

#if BITCHAT_DEBUG
  #define BITCHAT_DEBUG_PRINTLN(F, ...) Serial.printf("BITCHAT_BRIDGE: " F "\n", ##__VA_ARGS__)
#else
  #define BITCHAT_DEBUG_PRINTLN(...) {}
#endif

// PKCS#7 padding for Bitchat protocol signing (must match Android/iOS)
// Block sizes: 256, 512, 1024, 2048 bytes
static size_t applyPKCS7Padding(uint8_t* buffer, size_t dataLen, size_t bufferCapacity) {
    // Find optimal block size
    static const size_t blockSizes[] = {256, 512, 1024, 2048};
    size_t targetSize = dataLen;  // Default to no padding if too large

    for (size_t i = 0; i < 4; i++) {
        if (dataLen + 16 <= blockSizes[i]) {  // +16 for encryption overhead consideration
            targetSize = blockSizes[i];
            break;
        }
    }

    // Don't pad if already at or exceeding target, or if buffer too small
    if (dataLen >= targetSize || targetSize > bufferCapacity) {
        return dataLen;
    }

    size_t paddingNeeded = targetSize - dataLen;
    if (paddingNeeded > 255) {
        return dataLen;  // PKCS#7 can only encode padding length in 1 byte
    }

    // PKCS#7: all padding bytes equal the padding length
    for (size_t i = dataLen; i < targetSize; i++) {
        buffer[i] = static_cast<uint8_t>(paddingNeeded);
    }

    return targetSize;
}

BitchatBridge::BitchatBridge(mesh::Mesh& mesh, mesh::LocalIdentity& identity, const char* nodeName)
    : _mesh(mesh)
    , _identity(identity)
    , _nodeName(nodeName)
    , _bitchatPeerId(0)
    , _channelConfigured(false)
    , _lastAnnounceTime(0)
    , _pendingAnnounce(false)
    , _timeOffset(0)
    , _timeSynced(false)
    , _messagesRelayed(0)
    , _duplicatesDropped(0)
    , _meshChannelConfigured(false)
    , _meshChannelIndex(-1)
    , _messageHistoryHead(0)
{
    memset(_defaultChannelName, 0, sizeof(_defaultChannelName));
    strcpy(_defaultChannelName, "mesh");  // Default channel
    memset(&_meshcoreChannel, 0, sizeof(_meshcoreChannel));
    memset(&_meshChannel, 0, sizeof(_meshChannel));
    memset(_noisePublicKey, 0, sizeof(_noisePublicKey));

    // Initialize channel mappings
    for (size_t i = 0; i < MAX_CHANNEL_MAPPINGS; i++) {
        _channelMappings[i].configured = false;
        memset(_channelMappings[i].bitchatName, 0, sizeof(_channelMappings[i].bitchatName));
    }

    // Initialize peer cache
    for (size_t i = 0; i < PEER_CACHE_SIZE; i++) {
        _peerCache[i].valid = false;
        _peerCache[i].peerId = 0;
        _peerCache[i].timestamp = 0;
        memset(_peerCache[i].nickname, 0, sizeof(_peerCache[i].nickname));
    }

    // Initialize fragment buffers
    for (size_t i = 0; i < MAX_FRAGMENT_BUFFERS; i++) {
        _fragmentBuffers[i].active = false;
        _fragmentBuffers[i].senderId = 0;
        _fragmentBuffers[i].fragmentId = 0;
        _fragmentBuffers[i].totalFragments = 0;
        _fragmentBuffers[i].receivedMask = 0;
        _fragmentBuffers[i].dataLen = 0;
        _fragmentBuffers[i].startTime = 0;
    }

    // Initialize message history cache
    for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; i++) {
        _messageHistory[i].valid = false;
        _messageHistory[i].addedTimeMs = 0;
    }
}

void BitchatBridge::begin() {
    // Derive Bitchat peer ID from Meshcore identity
    _bitchatPeerId = derivePeerId(_identity);

    // Derive Noise public key (Curve25519) from Ed25519 identity
    deriveNoisePublicKey(_identity.pub_key, _noisePublicKey);

    // Configure the #mesh channel for relaying
    configureMeshChannel();

    BITCHAT_DEBUG_PRINTLN("Bridge initialized, peer ID: %llX", _bitchatPeerId);
}

void BitchatBridge::configureMeshChannel() {
    // Build the #mesh GroupChannel from the pre-calculated key
    memset(&_meshChannel, 0, sizeof(_meshChannel));
    memcpy(_meshChannel.secret, MESH_CHANNEL_KEY, 16);

    // Compute the 1-byte hash used for lookup
    mesh::Utils::sha256(_meshChannel.hash, sizeof(_meshChannel.hash),
                        MESH_CHANNEL_KEY, 16);

    _meshChannelConfigured = true;
    _meshChannelIndex = -1;  // Not stored in mesh's channel array yet

    // Register the mapping for Bitchat <-> MeshCore
    registerChannelMapping("mesh", _meshChannel);

    BITCHAT_DEBUG_PRINTLN("#mesh channel configured for bridging");
}

bool BitchatBridge::isMeshChannel(const mesh::GroupChannel& channel) const {
    // Compare the channel secret (key) - first 16 bytes
    return memcmp(channel.secret, MESH_CHANNEL_KEY, 16) == 0;
}

void BitchatBridge::addToMessageHistory(const BitchatMessage& msg) {
    _messageHistory[_messageHistoryHead].msg = msg;
    _messageHistory[_messageHistoryHead].addedTimeMs = millis();
    _messageHistory[_messageHistoryHead].valid = true;
    _messageHistoryHead = (_messageHistoryHead + 1) % MESSAGE_HISTORY_SIZE;
}

// ============================================================================
// GCS Filter Implementation for REQUEST_SYNC
// ============================================================================

// GCS TLV types (from Android RequestSyncPacket.kt)
#define GCS_TLV_P      0x01  // Golomb-Rice parameter (1 byte)
#define GCS_TLV_N      0x02  // Number of elements (4 bytes BE)
#define GCS_TLV_DATA   0x03  // Encoded bitstream

bool BitchatBridge::parseGCSFilter(const uint8_t* payload, size_t len, GCSFilter& outFilter) {
    // Initialize filter with defaults
    outFilter.p = 0;
    outFilter.n = 0;
    outFilter.m = 0;
    outFilter.data = nullptr;
    outFilter.dataLen = 0;

    if (payload == nullptr || len < 3) {
        return false;
    }

    // Parse TLV structure
    size_t offset = 0;
    bool hasP = false, hasN = false, hasData = false;

    while (offset + 2 <= len) {
        uint8_t type = payload[offset++];
        uint8_t length = payload[offset++];

        if (offset + length > len) {
            break;  // Truncated TLV
        }

        switch (type) {
            case GCS_TLV_P:
                if (length >= 1) {
                    outFilter.p = payload[offset];
                    hasP = true;
                }
                break;

            case GCS_TLV_N:
                if (length >= 4) {
                    // Big-endian 4-byte integer
                    outFilter.n = (static_cast<uint32_t>(payload[offset]) << 24) |
                                  (static_cast<uint32_t>(payload[offset + 1]) << 16) |
                                  (static_cast<uint32_t>(payload[offset + 2]) << 8) |
                                  static_cast<uint32_t>(payload[offset + 3]);
                    hasN = true;
                }
                break;

            case GCS_TLV_DATA:
                outFilter.data = &payload[offset];
                outFilter.dataLen = length;
                hasData = true;
                break;

            default:
                // Unknown TLV type - skip
                break;
        }
        offset += length;
    }

    // Calculate M = N * 2^P
    if (hasP && hasN) {
        outFilter.m = outFilter.n << outFilter.p;
    }

    BITCHAT_DEBUG_PRINTLN("GCS filter: P=%d, N=%u, M=%u, dataLen=%zu",
                          outFilter.p, outFilter.n, outFilter.m, outFilter.dataLen);

    // Filter is valid if we have all required fields
    return hasP && hasN && hasData && outFilter.n > 0;
}

bool BitchatBridge::GCSFilter::mightContain(const uint8_t* packetId16) const {
    // Check if a packet ID might be in the GCS filter.
    //
    // GCS works by hashing items to a value in range [0, M), then Golomb-Rice
    // encoding the sorted differences. To check membership, we:
    // 1. Hash the packet ID to get value h in [0, M)
    // 2. Decode the filter to find all stored values
    // 3. Check if h is among the stored values
    //
    // For efficiency, we decode on-the-fly and stop early if we find or pass h.

    if (data == nullptr || dataLen == 0 || n == 0 || m == 0) {
        return false;  // Empty filter - nothing matches
    }

    // Hash packet ID to range [0, M) using SipHash-like reduction
    // Use first 8 bytes of packet ID as uint64, then reduce to M
    uint64_t h64 = 0;
    for (int i = 0; i < 8; i++) {
        h64 |= (static_cast<uint64_t>(packetId16[i]) << (i * 8));
    }

    // Reduce to range [0, M) using multiplication and shift (fast modulo)
    // h = (h64 * M) >> 64, but since M fits in 32 bits, we use simpler approach
    uint32_t h = static_cast<uint32_t>(h64 % m);

    // Golomb-Rice decode the filter to check membership
    // Each value is encoded as: unary(quotient) + binary(remainder, P bits)
    // Values are delta-encoded (differences from previous value)

    uint32_t current = 0;  // Running sum of deltas
    size_t bitPos = 0;     // Bit position in data

    // Helper to read a single bit
    auto readBit = [this, &bitPos]() -> int {
        if (bitPos / 8 >= dataLen) return -1;  // Past end
        int bit = (data[bitPos / 8] >> (7 - (bitPos % 8))) & 1;
        bitPos++;
        return bit;
    };

    // Helper to read P bits as binary value
    auto readBinary = [this, &bitPos](uint8_t bits) -> int32_t {
        if (bits == 0) return 0;
        if (bitPos / 8 + (bits + 7) / 8 > dataLen) return -1;

        int32_t value = 0;
        for (uint8_t i = 0; i < bits; i++) {
            int bit = (data[bitPos / 8] >> (7 - (bitPos % 8))) & 1;
            value = (value << 1) | bit;
            bitPos++;
        }
        return value;
    };

    for (uint32_t i = 0; i < n; i++) {
        // Decode quotient (unary: count 1s until 0)
        uint32_t quotient = 0;
        int bit;
        while ((bit = readBit()) == 1) {
            quotient++;
            if (quotient > m) return false;  // Malformed filter
        }
        if (bit < 0) return false;  // Truncated

        // Decode remainder (P bits, binary)
        int32_t remainder = readBinary(p);
        if (remainder < 0) return false;  // Truncated

        // Reconstruct delta and add to current
        uint32_t delta = (quotient << p) | static_cast<uint32_t>(remainder);
        current += delta;

        // Check if we found or passed the target
        if (current == h) {
            return true;  // Found - requester has this message
        }
        if (current > h) {
            return false;  // Passed it - requester doesn't have this message
        }
    }

    return false;  // Not found in filter
}

void BitchatBridge::handleRequestSync(const BitchatMessage& msg) {
    BITCHAT_DEBUG_PRINTLN("REQUEST_SYNC from %llX", (unsigned long long)msg.getSenderId64());

    // Parse the GCS filter from the REQUEST_SYNC payload
    // The filter tells us which messages the requester already has
    GCSFilter filter;
    bool hasFilter = parseGCSFilter(msg.payload, msg.payloadLength, filter);

    if (hasFilter) {
        BITCHAT_DEBUG_PRINTLN("REQUEST_SYNC has GCS filter (N=%u elements)", filter.n);
    } else {
        BITCHAT_DEBUG_PRINTLN("REQUEST_SYNC has no GCS filter - sending all");
    }

    // Expire old messages before responding
    uint32_t now = millis();
    for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; i++) {
        if (_messageHistory[i].valid &&
            (now - _messageHistory[i].addedTimeMs) > MESSAGE_EXPIRY_MS) {
            _messageHistory[i].valid = false;
            BITCHAT_DEBUG_PRINTLN("Expired old message at index %zu", i);
        }
    }

    // Send cached messages that the requester doesn't have
    int sent = 0;
    int skipped = 0;
    for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; i++) {
        if (!_messageHistory[i].valid) continue;

        // If we have a filter, check if requester already has this message
        if (hasFilter) {
            uint8_t packetId[16];
            BitchatProtocol::computePacketId(_messageHistory[i].msg, packetId);

            if (filter.mightContain(packetId)) {
                // Requester likely already has this message - skip it
                skipped++;
                BITCHAT_DEBUG_PRINTLN("Skipping msg %zu - already in filter", i);
                continue;
            }
        }

#ifdef ESP32
        _bleService.broadcastMessage(_messageHistory[i].msg);
        sent++;
#endif
    }

    // Always send our announcement
    sendPeerAnnouncement();

    BITCHAT_DEBUG_PRINTLN("REQUEST_SYNC response: sent %d messages, skipped %d (filter=%s)",
                          sent, skipped, hasFilter ? "yes" : "no");
}

uint64_t BitchatBridge::derivePeerId(const mesh::LocalIdentity& identity) {
    // Use first 8 bytes of public key as peer ID
    uint64_t id = 0;
    for (int i = 0; i < 8; i++) {
        id |= (static_cast<uint64_t>(identity.pub_key[i]) << (i * 8));
    }
    return id;
}

void BitchatBridge::deriveNoisePublicKey(const uint8_t* ed25519PubKey, uint8_t* curve25519PubKey) {
    // Convert Ed25519 public key (edwards Y) to Curve25519 public key (montgomery X)
    // Formula: montgomeryX = (edwardsY + 1) * inverse(1 - edwardsY) mod p
    // This is the standard Ed25519→Curve25519 conversion from RFC 7748
    fe x1, tmp0, tmp1;

    fe_frombytes(x1, ed25519PubKey);
    fe_1(tmp1);
    fe_add(tmp0, x1, tmp1);      // tmp0 = edwardsY + 1
    fe_sub(tmp1, tmp1, x1);      // tmp1 = 1 - edwardsY
    fe_invert(tmp1, tmp1);       // tmp1 = inverse(1 - edwardsY)
    fe_mul(x1, tmp0, tmp1);      // x1 = (edwardsY + 1) * inverse(1 - edwardsY)
    fe_tobytes(curve25519PubKey, x1);
}

void BitchatBridge::loop() {
#ifdef ESP32
    _bleService.loop();

    uint32_t now = millis();

    // Handle deferred announcement (from BLE callback - limited stack)
    if (_pendingAnnounce) {
        _pendingAnnounce = false;
        sendPeerAnnouncement();
        _lastAnnounceTime = now;
    }

    // Periodically expire old messages from cache (every 30 seconds)
    static uint32_t lastExpiryCheck = 0;
    if (now - lastExpiryCheck >= 30000) {
        lastExpiryCheck = now;
        for (size_t i = 0; i < MESSAGE_HISTORY_SIZE; i++) {
            if (_messageHistory[i].valid &&
                (now - _messageHistory[i].addedTimeMs) > MESSAGE_EXPIRY_MS) {
                _messageHistory[i].valid = false;
            }
        }
    }

    // Always send periodic announcements - don't check if service is active
    // This ensures announcements resume after MeshCore app disconnects
    // BLE notification will go out whether or not anyone is listening
    // Use shorter interval when we know a client has interacted
    uint32_t interval = _bleService.hasConnectedClient()
        ? ANNOUNCE_INTERVAL_CONNECTED_MS
        : ANNOUNCE_INTERVAL_MS;

    if (now - _lastAnnounceTime >= interval) {
        BITCHAT_DEBUG_PRINTLN("Sending periodic announcement (interval=%lu, elapsed=%lu)",
            interval, now - _lastAnnounceTime);
        sendPeerAnnouncement();
        _lastAnnounceTime = now;
    }
#endif
}

#ifdef ESP32
bool BitchatBridge::attachBLEService(BLEServer* server) {
    if (!_bleService.attachToServer(server, this)) {
        BITCHAT_DEBUG_PRINTLN("Failed to attach BLE service");
        return false;
    }
    _bleService.start();

    // Send first announcement immediately so connecting clients see us right away
    sendPeerAnnouncement();
    _lastAnnounceTime = millis();

    return true;
}

bool BitchatBridge::beginStandalone(const char* deviceName) {
    // Initialize BLE independently (no SerialBLEInterface)
    BLEDevice::init(deviceName);
    BLEDevice::setMTU(185);

    // Create BLE server
    BLEServer* server = BLEDevice::createServer();
    if (server == nullptr) {
        return false;
    }

    // Attach Bitchat service to the server
    if (!_bleService.attachToServer(server, this)) {
        return false;
    }

    // Set device name and start service (without touching advertising)
    _bleService.setDeviceName(deviceName);
    _bleService.startServiceOnly();

    // Start advertising with Bitchat UUID in main advertisement
    _bleService.startAdvertising();

    // Send first announcement
    sendPeerAnnouncement();
    _lastAnnounceTime = millis();

    BITCHAT_DEBUG_PRINTLN("Standalone mode initialized: %s", deviceName);
    return true;
}

bool BitchatBridge::isBLEActive() const {
    return _bleService.isActive();
}

bool BitchatBridge::hasBitchatClient() const {
    return _bleService.hasConnectedClient();
}
#else
bool BitchatBridge::isBLEActive() const {
    return false;
}

bool BitchatBridge::hasBitchatClient() const {
    return false;
}
#endif

void BitchatBridge::setDefaultChannel(const char* channelName) {
    strncpy(_defaultChannelName, channelName, sizeof(_defaultChannelName) - 1);
    _defaultChannelName[sizeof(_defaultChannelName) - 1] = '\0';
}

void BitchatBridge::setMeshcoreChannel(const mesh::GroupChannel& channel) {
    _meshcoreChannel = channel;
    _channelConfigured = true;
}

bool BitchatBridge::registerChannelMapping(const char* bitchatChannelName, const mesh::GroupChannel& meshChannel) {
    // Skip # prefix if present
    const char* name = bitchatChannelName;
    if (name[0] == '#') name++;

    // Check if mapping already exists (update it)
    for (size_t i = 0; i < MAX_CHANNEL_MAPPINGS; i++) {
        if (_channelMappings[i].configured &&
            strcmp(_channelMappings[i].bitchatName, name) == 0) {
            _channelMappings[i].meshChannel = meshChannel;
            return true;
        }
    }

    // Find empty slot
    for (size_t i = 0; i < MAX_CHANNEL_MAPPINGS; i++) {
        if (!_channelMappings[i].configured) {
            strncpy(_channelMappings[i].bitchatName, name, sizeof(_channelMappings[i].bitchatName) - 1);
            _channelMappings[i].bitchatName[sizeof(_channelMappings[i].bitchatName) - 1] = '\0';
            _channelMappings[i].meshChannel = meshChannel;
            _channelMappings[i].configured = true;
            BITCHAT_DEBUG_PRINTLN("Registered channel mapping: %s", name);
            return true;
        }
    }

    BITCHAT_DEBUG_PRINTLN("Channel mapping registry full");
    return false;
}

bool BitchatBridge::findMeshChannel(const char* channelName, mesh::GroupChannel& outChannel) {
    // Skip # prefix if present
    const char* name = channelName;
    if (name[0] == '#') name++;

    // Search in registry
    for (size_t i = 0; i < MAX_CHANNEL_MAPPINGS; i++) {
        if (_channelMappings[i].configured &&
            strcmp(_channelMappings[i].bitchatName, name) == 0) {
            outChannel = _channelMappings[i].meshChannel;
            return true;
        }
    }

    // Fall back to default channel if configured
    if (_channelConfigured) {
        outChannel = _meshcoreChannel;
        return true;
    }

    return false;
}

const char* BitchatBridge::getChannelName(const mesh::GroupChannel& channel) {
    // Search in registry
    for (size_t i = 0; i < MAX_CHANNEL_MAPPINGS; i++) {
        if (_channelMappings[i].configured &&
            memcmp(&_channelMappings[i].meshChannel, &channel, sizeof(mesh::GroupChannel)) == 0) {
            return _channelMappings[i].bitchatName;
        }
    }

    // Return default channel name
    return _defaultChannelName;
}

void BitchatBridge::syncTimeFromPacket(uint64_t packetTimestamp) {
#ifdef ARDUINO
    // Only sync if the timestamp looks reasonable (after year 2024, before year 2100)
    // Unix timestamp for 2024-01-01 00:00:00 UTC = 1704067200000 ms
    // Unix timestamp for 2100-01-01 00:00:00 UTC = 4102444800000 ms
    const uint64_t MIN_VALID_TIMESTAMP = 1704067200000ULL;  // 2024-01-01
    const uint64_t MAX_VALID_TIMESTAMP = 4102444800000ULL;  // 2100-01-01

    // Threshold for RTC sync - if RTC differs by more than this, update it
    // This allows Bitchat to act as an NTP-like time source for the mesh
    const uint32_t RTC_SYNC_THRESHOLD_SECS = 30;  // 30 seconds

    if (packetTimestamp >= MIN_VALID_TIMESTAMP && packetTimestamp <= MAX_VALID_TIMESTAMP) {
        uint64_t localMs = millis();
        int64_t newOffset = static_cast<int64_t>(packetTimestamp) - static_cast<int64_t>(localMs);

        // If this is first sync, or if the offset changed significantly (device was rebooted), update it
        if (!_timeSynced || abs(newOffset - _timeOffset) > 60000) {  // > 1 minute drift
            _timeOffset = newOffset;
            _timeSynced = true;
            BITCHAT_DEBUG_PRINTLN("Time synced from Bitchat: offset=%lld ms", (long long)_timeOffset);
        }

        // Also sync the RTC if the difference is significant
        // This allows other MeshCore components to benefit from Bitchat time sync
        mesh::RTCClock* rtc = _mesh.getRTCClock();
        if (rtc != nullptr) {
            uint32_t bitchatTimeSecs = static_cast<uint32_t>(packetTimestamp / 1000ULL);
            uint32_t rtcTime = rtc->getCurrentTime();
            int32_t timeDiff = static_cast<int32_t>(bitchatTimeSecs) - static_cast<int32_t>(rtcTime);

            if (abs(timeDiff) > RTC_SYNC_THRESHOLD_SECS) {
                rtc->setCurrentTime(bitchatTimeSecs);
                BITCHAT_DEBUG_PRINTLN("RTC synced from Bitchat: %u (was off by %d secs)",
                                      bitchatTimeSecs, timeDiff);
            }
        }
    }
#endif
}

bool BitchatBridge::parseAnnounceTLV(const uint8_t* payload, size_t len, char* nickname, size_t nickLen) {
    // ANNOUNCE payload is TLV encoded: [type:1][length:1][value:N]...
    size_t offset = 0;
    while (offset + 2 <= len) {
        uint8_t type = payload[offset++];
        uint8_t length = payload[offset++];
        if (offset + length > len) break;

        if (type == BITCHAT_TLV_NICKNAME && length > 0) {
            size_t toCopy = (length < nickLen - 1) ? length : nickLen - 1;
            memcpy(nickname, &payload[offset], toCopy);
            nickname[toCopy] = '\0';
            return true;
        }
        offset += length;
    }
    return false;
}

void BitchatBridge::cachePeer(uint64_t peerId, const char* nickname) {
    uint32_t now = millis();

    // First, check if peer already exists and update it
    for (size_t i = 0; i < PEER_CACHE_SIZE; i++) {
        if (_peerCache[i].valid && _peerCache[i].peerId == peerId) {
            strncpy(_peerCache[i].nickname, nickname, sizeof(_peerCache[i].nickname) - 1);
            _peerCache[i].nickname[sizeof(_peerCache[i].nickname) - 1] = '\0';
            _peerCache[i].timestamp = now;
            BITCHAT_DEBUG_PRINTLN("Updated peer cache: %s -> %016llX", nickname, (unsigned long long)peerId);
            return;
        }
    }

    // Find an empty slot or the oldest entry
    size_t targetIdx = 0;
    uint32_t oldestTime = UINT32_MAX;
    for (size_t i = 0; i < PEER_CACHE_SIZE; i++) {
        if (!_peerCache[i].valid) {
            targetIdx = i;
            break;
        }
        if (_peerCache[i].timestamp < oldestTime) {
            oldestTime = _peerCache[i].timestamp;
            targetIdx = i;
        }
    }

    // Store the new peer
    _peerCache[targetIdx].peerId = peerId;
    strncpy(_peerCache[targetIdx].nickname, nickname, sizeof(_peerCache[targetIdx].nickname) - 1);
    _peerCache[targetIdx].nickname[sizeof(_peerCache[targetIdx].nickname) - 1] = '\0';
    _peerCache[targetIdx].timestamp = now;
    _peerCache[targetIdx].valid = true;
    BITCHAT_DEBUG_PRINTLN("Cached new peer: %s -> %016llX", nickname, (unsigned long long)peerId);
}

const char* BitchatBridge::lookupPeerNickname(uint64_t peerId) {
    for (size_t i = 0; i < PEER_CACHE_SIZE; i++) {
        if (_peerCache[i].valid && _peerCache[i].peerId == peerId) {
            return _peerCache[i].nickname;
        }
    }
    return nullptr;
}

// Compile-time timestamp calculation (approximate)
// __DATE__ format: "Jan 16 2025"
// __TIME__ format: "10:30:45"
static uint64_t getCompileTimeMs() {
    // Parse __DATE__ and __TIME__ to get approximate compile timestamp
    // This is a rough estimate but good enough for our purposes
    const char* date = __DATE__;  // "Mmm DD YYYY"
    const char* time = __TIME__;  // "HH:MM:SS"

    // Month lookup
    int month = 0;
    if (date[0] == 'J' && date[1] == 'a') month = 1;       // Jan
    else if (date[0] == 'F') month = 2;                     // Feb
    else if (date[0] == 'M' && date[2] == 'r') month = 3;  // Mar
    else if (date[0] == 'A' && date[1] == 'p') month = 4;  // Apr
    else if (date[0] == 'M' && date[2] == 'y') month = 5;  // May
    else if (date[0] == 'J' && date[2] == 'n') month = 6;  // Jun
    else if (date[0] == 'J' && date[2] == 'l') month = 7;  // Jul
    else if (date[0] == 'A' && date[1] == 'u') month = 8;  // Aug
    else if (date[0] == 'S') month = 9;                     // Sep
    else if (date[0] == 'O') month = 10;                    // Oct
    else if (date[0] == 'N') month = 11;                    // Nov
    else if (date[0] == 'D') month = 12;                    // Dec

    int day = (date[4] == ' ' ? 0 : (date[4] - '0') * 10) + (date[5] - '0');
    int year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 +
               (date[9] - '0') * 10 + (date[10] - '0');

    int hour = (time[0] - '0') * 10 + (time[1] - '0');
    int minute = (time[3] - '0') * 10 + (time[4] - '0');
    int second = (time[6] - '0') * 10 + (time[7] - '0');

    // Calculate Unix timestamp (simplified, ignoring leap years for rough estimate)
    // Days since Unix epoch (Jan 1, 1970)
    int daysPerMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    long days = (year - 1970) * 365 + (year - 1969) / 4;  // Leap year approximation
    for (int m = 1; m < month; m++) {
        days += daysPerMonth[m];
    }
    days += day - 1;

    uint64_t seconds = days * 86400ULL + hour * 3600 + minute * 60 + second;
    return seconds * 1000ULL;
}

uint64_t BitchatBridge::getCurrentTimeMs() {
#ifdef ARDUINO
    // If we have synced time from a Bitchat client, use that (most reliable)
    if (_timeSynced) {
        return static_cast<uint64_t>(static_cast<int64_t>(millis()) + _timeOffset);
    }

    // Get compile time as minimum valid timestamp
    static uint64_t compileTimeMs = getCompileTimeMs();

    // Fall back to RTC if available AND recent
    mesh::RTCClock* rtc = _mesh.getRTCClock();
    if (rtc != nullptr) {
        uint32_t rtcTime = rtc->getCurrentTime();
        uint64_t rtcTimeMs = static_cast<uint64_t>(rtcTime) * 1000ULL;
        // Only use RTC if it's at least as recent as compile time
        if (rtcTimeMs >= compileTimeMs) {
            return rtcTimeMs;
        }
    }

    // Use compile time + millis() as fallback
    // This gives us a timestamp that's at least as recent as when the firmware was built
    return compileTimeMs + millis();
#else
    return 0;
#endif
}

void BitchatBridge::sendPeerAnnouncement() {
#ifdef ESP32
    uint64_t timestamp = getCurrentTimeMs();

    BitchatMessage msg;
    BitchatProtocol::createAnnounce(
        msg,
        _bitchatPeerId,
        _nodeName,
        _noisePublicKey,      // Curve25519 for Noise protocol
        _identity.pub_key,    // Ed25519 for signatures
        timestamp,
        DEFAULT_TTL
    );

    // Sign the announce - Android requires signatures
    signMessage(msg);

    _bleService.broadcastMessage(msg);
    BITCHAT_DEBUG_PRINTLN("Sent peer announcement");
#endif
}

void BitchatBridge::signMessage(BitchatMessage& msg) {
#ifdef ESP32
    // IMPORTANT: Bitchat protocol signs with TTL=0 and signature flag cleared
    // AND applies PKCS#7 padding to match Android/iOS toBinaryDataForSigning() behavior
    uint8_t originalTtl = msg.ttl;
    msg.ttl = 0;  // Fixed TTL for signing (matches SYNC_TTL_HOPS)
    msg.setHasSignature(false);  // Clear signature flag for signing

    // Use larger buffer for padding
    uint8_t signData[512];
    size_t signLen = BitchatProtocol::serializeMessage(msg, signData, sizeof(signData));
    if (signLen > 0) {
        // Apply PKCS#7 padding to match Android/iOS block sizes
        size_t paddedLen = applyPKCS7Padding(signData, signLen, sizeof(signData));

        BITCHAT_DEBUG_PRINTLN("Signing message: %zu bytes (padded from %zu)", paddedLen, signLen);

        _identity.sign(msg.signature, signData, paddedLen);
    }

    // Restore actual TTL and set signature flag for transmission
    msg.ttl = originalTtl;
    msg.setHasSignature(true);
#endif
}

// ============================================================================
// Bitchat → Meshcore
// ============================================================================

#ifdef ESP32
void BitchatBridge::onBitchatMessageReceived(const BitchatMessage& msg) {
    processBitchatMessage(msg);
}

void BitchatBridge::onBitchatClientConnect() {
    BITCHAT_DEBUG_PRINTLN("Client connected");

    // Send announcement immediately when client connects
    // This is now called from loop() so it's safe to do heavy work
    sendPeerAnnouncement();
    _lastAnnounceTime = millis();
}

void BitchatBridge::onBitchatClientDisconnect() {
    BITCHAT_DEBUG_PRINTLN("Bitchat client disconnected");
}
#endif

void BitchatBridge::processBitchatMessage(const BitchatMessage& msg) {
    // Sync time from incoming Bitchat packets (Android sends valid Unix timestamps)
    // This is critical: our announces will be rejected as stale without valid time
    if (msg.timestamp > 0) {
        syncTimeFromPacket(msg.timestamp);
    }

    // Check for duplicates
    if (_duplicateCache.isDuplicate(msg)) {
        _duplicatesDropped++;
        BITCHAT_DEBUG_PRINTLN("Duplicate message dropped");
        return;
    }

    // Handle based on message type
    switch (msg.type) {
        case BITCHAT_MSG_MESSAGE: {
            char senderNick[64];
            char content[512];  // Increased to handle decompressed messages (up to ~500 bytes)
            char channelName[32];
            bool parsedAsTlv = false;

            // First try TLV parsing (some messages might use it)
            bool parsed = parseBitchatMessageTLV(msg.payload, msg.payloadLength,
                                                  senderNick, sizeof(senderNick),
                                                  content, sizeof(content),
                                                  channelName, sizeof(channelName));
            if (parsed) {
                parsedAsTlv = true;
            }

            if (!parsed && msg.payloadLength > 0 && msg.payloadLength < sizeof(content)) {
                // TLV parsing failed - treat payload as plain text
                // This is the simple format Bitchat uses for channel messages
                memcpy(content, msg.payload, msg.payloadLength);
                content[msg.payloadLength] = '\0';

                // Try to look up cached nickname from previous ANNOUNCE
                uint64_t senderId = msg.getSenderId64();
                const char* cachedNick = lookupPeerNickname(senderId);
                if (cachedNick != nullptr) {
                    strncpy(senderNick, cachedNick, sizeof(senderNick) - 1);
                    senderNick[sizeof(senderNick) - 1] = '\0';
                } else {
                    // Fall back to ID-based nickname
                    snprintf(senderNick, sizeof(senderNick), "%04X",
                             (unsigned)(senderId & 0xFFFF));
                }

                // Plain text messages are assumed to be #mesh channel messages
                // The outer HAS_RECIPIENT flag doesn't indicate DM for plain text
                strcpy(channelName, BITCHAT_MESH_CHANNEL);

                BITCHAT_DEBUG_PRINTLN("Plain text message from %s: %s", senderNick, content);
                parsed = true;
            }

            if (parsed) {
                // IMPORTANT: Only relay #mesh channel messages, ignore everything else
                // Check if channel matches #mesh (with or without # prefix)
                const char* chanToCheck = channelName;
                if (chanToCheck[0] == '#') chanToCheck++;

                if (strcmp(chanToCheck, "mesh") != 0) {
                    BITCHAT_DEBUG_PRINTLN("Ignoring message to channel '%s' (only #mesh)", channelName);
                    break;
                }

                // Ignore DMs - only check for TLV-parsed messages
                // Plain text messages use outer HAS_RECIPIENT for signing, not for DM indication
                if (parsedAsTlv && msg.hasRecipient()) {
                    BITCHAT_DEBUG_PRINTLN("Ignoring DM (only #mesh channel is bridged)");
                    break;
                }

                // Add to message history for REQUEST_SYNC responses
                addToMessageHistory(msg);

                // Relay to MeshCore #mesh channel
                BITCHAT_DEBUG_PRINTLN("Relaying message from %s to #mesh", senderNick);
                relayChannelMessageToMesh(msg, channelName, senderNick, content);
            } else {
                BITCHAT_DEBUG_PRINTLN("Failed to parse MESSAGE payload (len=%u)", msg.payloadLength);
            }
            break;
        }

        case BITCHAT_MSG_ANNOUNCE: {
            // Parse announce to extract and cache peer's nickname
            char nickname[16];
            if (parseAnnounceTLV(msg.payload, msg.payloadLength, nickname, sizeof(nickname))) {
                uint64_t peerId = msg.getSenderId64();
                cachePeer(peerId, nickname);
                BITCHAT_DEBUG_PRINTLN("Cached peer: %s (%llX)", nickname, (unsigned long long)peerId);
            }
            break;
        }

        case BITCHAT_MSG_PING:
            // Respond with PONG
            BITCHAT_DEBUG_PRINTLN("Received ping, sending pong");
            {
                BitchatMessage pong;
                pong.version = BITCHAT_VERSION;
                pong.type = BITCHAT_MSG_PONG;
                pong.ttl = 1;
                pong.timestamp = getCurrentTimeMs();
                pong.flags = BITCHAT_FLAG_HAS_RECIPIENT;
                pong.setSenderId64(_bitchatPeerId);
                pong.setRecipientId64(msg.getSenderId64());
                pong.payloadLength = 0;
#ifdef ESP32
                _bleService.broadcastMessage(pong);
#endif
            }
            break;

        case BITCHAT_MSG_FILE_TRANSFER:
            // File transfers (images, etc.) are not supported on mesh
            BITCHAT_DEBUG_PRINTLN("Skipping file transfer (not supported)");
            break;

        case BITCHAT_MSG_FRAGMENT_NEW:
        case BITCHAT_MSG_FRAGMENT:
            // Fragment messages are used for long text messages (>245 bytes)
            // Reassemble and process when complete
            handleFragment(msg);
            break;

        case BITCHAT_MSG_REQUEST_SYNC:
            handleRequestSync(msg);
            break;

        default:
            BITCHAT_DEBUG_PRINTLN("Unhandled message type: 0x%02X", msg.type);
            break;
    }
}

bool BitchatBridge::parseBitchatMessageTLV(const uint8_t* payload, size_t payloadLen,
                                            char* senderNick, size_t senderNickLen,
                                            char* content, size_t contentLen,
                                            char* channelName, size_t channelNameLen) {
    // Minimum size: flags(1) + timestamp(8) + idLen(1) + senderLen(1) + contentLen(2) = 13 bytes
    if (payloadLen < 13 || senderNickLen == 0 || contentLen == 0 || channelNameLen == 0) {
        return false;
    }

    senderNick[0] = '\0';
    content[0] = '\0';
    channelName[0] = '\0';

    size_t offset = 0;

    // Read flags byte
    uint8_t flags = payload[offset++];
    bool hasOriginalSender = (flags & 0x04) != 0;
    bool hasRecipientNickname = (flags & 0x08) != 0;
    bool hasSenderPeerID = (flags & 0x10) != 0;
    bool hasMentions = (flags & 0x20) != 0;
    bool hasChannel = (flags & 0x40) != 0;
    bool isEncrypted = (flags & 0x80) != 0;

    // Skip timestamp (8 bytes big-endian)
    if (offset + 8 > payloadLen) {
        return false;
    }
    offset += 8;

    // Read ID length and skip ID
    if (offset >= payloadLen) {
        return false;
    }
    uint8_t idLen = payload[offset++];
    if (offset + idLen > payloadLen) {
        return false;
    }
    offset += idLen;

    // Read sender nickname
    if (offset >= payloadLen) {
        return false;
    }
    uint8_t senderLen = payload[offset++];
    if (offset + senderLen > payloadLen) {
        return false;
    }

    size_t toCopy = senderLen;
    if (toCopy >= senderNickLen) toCopy = senderNickLen - 1;
    memcpy(senderNick, &payload[offset], toCopy);
    senderNick[toCopy] = '\0';
    offset += senderLen;

    // Read content length (2 bytes big-endian)
    if (offset + 2 > payloadLen) {
        return false;
    }
    uint16_t contentLength = (static_cast<uint16_t>(payload[offset]) << 8) | payload[offset + 1];
    offset += 2;

    // Read content
    if (offset + contentLength > payloadLen) {
        return false;
    }
    if (!isEncrypted) {
        toCopy = contentLength;
        if (toCopy >= contentLen) toCopy = contentLen - 1;
        memcpy(content, &payload[offset], toCopy);
        content[toCopy] = '\0';
    }
    offset += contentLength;

    // Skip optional fields to get to channel
    // Order: originalSender, recipientNickname, senderPeerID, mentions, channel

    if (hasOriginalSender && offset < payloadLen) {
        uint8_t len = payload[offset++];
        if (offset + len > payloadLen) return false;
        offset += len;
    }

    if (hasRecipientNickname && offset < payloadLen) {
        uint8_t len = payload[offset++];
        if (offset + len > payloadLen) return false;
        offset += len;
    }

    if (hasSenderPeerID && offset < payloadLen) {
        uint8_t len = payload[offset++];
        if (offset + len > payloadLen) return false;
        offset += len;
    }

    if (hasMentions && offset < payloadLen) {
        uint8_t mentionCount = payload[offset++];
        for (uint8_t i = 0; i < mentionCount && offset < payloadLen; i++) {
            uint8_t len = payload[offset++];
            if (offset + len > payloadLen) return false;
            offset += len;
        }
    }

    // Read channel if present
    if (hasChannel && offset < payloadLen) {
        uint8_t chanLen = payload[offset++];
        if (offset + chanLen > payloadLen) return false;

        toCopy = chanLen;
        if (toCopy >= channelNameLen) toCopy = channelNameLen - 1;
        memcpy(channelName, &payload[offset], toCopy);
        channelName[toCopy] = '\0';
    }

    BITCHAT_DEBUG_PRINTLN("TLV parsed: sender='%s' content='%s' channel='%s'", senderNick, content, channelName);

    return senderNick[0] != '\0';  // At minimum we need a sender
}

void BitchatBridge::sendSingleMessageToMesh(const char* senderNick, const char* text, uint32_t delay_millis) {
    // This is the internal function that sends a single message chunk to the mesh.
    // The caller is responsible for message splitting if needed.

    // Must have #mesh channel configured
    if (!_meshChannelConfigured) {
        Serial.println("BITCHAT: #mesh channel not configured, cannot send to mesh");
        return;
    }

    // Use the #mesh channel for all bridged messages
    mesh::GroupChannel targetChannel = _meshChannel;

    // Get timestamp - prefer synced Bitchat time over RTC
    // MeshCore uses Unix seconds, Bitchat uses Unix milliseconds
    uint32_t timestamp = 0;
    if (_timeSynced) {
        // Use synced Bitchat time (convert from ms to seconds)
        timestamp = static_cast<uint32_t>(getCurrentTimeMs() / 1000ULL);
    } else {
        mesh::RTCClock* rtc = _mesh.getRTCClock();
        timestamp = rtc ? rtc->getCurrentTime() : 0;
    }

    // Build Meshcore group message payload
    // Format: timestamp(4) + txt_type(1) + "📱 sender: text"
    uint8_t payload[MAX_PACKET_PAYLOAD];
    size_t offset = 0;

    // Timestamp (4 bytes)
    memcpy(&payload[offset], &timestamp, 4);
    offset += 4;

    // Text type (0 = plain text)
    payload[offset++] = 0;

    // Add 📱 prefix to sender name (identifies Bitchat origin)
    // 📱 = UTF-8: F0 9F 93 B1 (4 bytes)
    char prefixedSender[68];  // 4 bytes emoji + 1 space + 63 chars max
    snprintf(prefixedSender, sizeof(prefixedSender), "\xF0\x9F\x93\xB1 %s", senderNick);

    size_t senderLen = strlen(prefixedSender);
    size_t textLen = strlen(text);

    // Copy "📱 sender: "
    size_t available = MAX_PACKET_PAYLOAD - offset - 1;
    size_t toCopy = senderLen;
    if (toCopy > available) toCopy = available;
    memcpy(&payload[offset], prefixedSender, toCopy);
    offset += toCopy;

    if (offset < MAX_PACKET_PAYLOAD - 2) {
        payload[offset++] = ':';
        payload[offset++] = ' ';
    }

    // Copy text
    available = MAX_PACKET_PAYLOAD - offset - 1;
    toCopy = textLen;
    if (toCopy > available) toCopy = available;
    memcpy(&payload[offset], text, toCopy);
    offset += toCopy;

    // Null terminate
    payload[offset] = '\0';

    // Create and send packet with optional delay (non-blocking queue)
    mesh::Packet* pkt = _mesh.createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, targetChannel, payload, offset);
    if (pkt != nullptr) {
        _mesh.sendFlood(pkt, delay_millis);
        _messagesRelayed++;
        BITCHAT_DEBUG_PRINTLN("Sent to mesh: %s: %s (delay=%lu)", prefixedSender, text, delay_millis);
    } else {
        BITCHAT_DEBUG_PRINTLN("Failed to create mesh packet");
    }
}

void BitchatBridge::relayChannelMessageToMesh(const BitchatMessage& msg, const char* channelName,
                                               const char* senderNick, const char* text) {
    // Find the MeshCore channel for this Bitchat channel
    mesh::GroupChannel targetChannel;
    if (!findMeshChannel(channelName, targetChannel)) {
        BITCHAT_DEBUG_PRINTLN("No channel mapping for '%s', cannot relay to mesh", channelName);
        return;
    }

    // Calculate available space for message text
    // MeshCore MAX_TEXT_LEN is 160 bytes total for: "📱nick: text"
    // Overhead: 📱(4) + space(1) + nick(up to 13) + ": "(2) = ~20 bytes
    // With part indicator "[X/Y] "(7 bytes), we have ~133 bytes for text
    // Be conservative and use 120 bytes per chunk
    const size_t MAX_CHUNK_SIZE = 120;

    size_t contentLen = strlen(text);

    if (contentLen <= MAX_CHUNK_SIZE) {
        // Single message - no splitting needed
        sendSingleMessageToMesh(senderNick, text, 0);
        return;
    }

    // Calculate number of parts needed
    int numParts = (contentLen + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
    if (numParts > 9) {
        numParts = 9;  // Cap at 9 parts to keep indicator short
    }

    BITCHAT_DEBUG_PRINTLN("Splitting message from %s into %d parts (len=%d)", senderNick, numParts, (int)contentLen);

    // Send each part with staggered delays (non-blocking - uses mesh queue)
    // 2 second delay between parts to avoid flooding the mesh
    const uint32_t PART_DELAY_MS = 2000;

    size_t offset = 0;
    for (int part = 0; part < numParts && offset < contentLen; part++) {
        size_t remaining = contentLen - offset;
        size_t chunkLen = (remaining > MAX_CHUNK_SIZE) ? MAX_CHUNK_SIZE : remaining;

        // Adjust chunk length to avoid splitting UTF-8 multibyte characters
        // UTF-8 continuation bytes start with 10xxxxxx (0x80-0xBF)
        while (chunkLen > 0 && chunkLen < remaining) {
            uint8_t nextByte = (uint8_t)text[offset + chunkLen];
            if ((nextByte & 0xC0) != 0x80) {
                // This is not a continuation byte, safe to split here
                break;
            }
            // Back up to avoid splitting mid-character
            chunkLen--;
        }

        if (chunkLen == 0) {
            BITCHAT_DEBUG_PRINTLN("Error: Could not find safe UTF-8 split point");
            break;
        }

        // Build chunk with part indicator
        char chunk[180];  // Room for part indicator + text
        snprintf(chunk, sizeof(chunk), "[%d/%d] %.*s",
                 part + 1, numParts, (int)chunkLen, text + offset);

        BITCHAT_DEBUG_PRINTLN("Part %d/%d: offset=%d, len=%d, delay=%lu",
                              part + 1, numParts, (int)offset, (int)chunkLen, (unsigned long)(part * PART_DELAY_MS));

        // Use staggered delays: 0ms, 2000ms, 4000ms, etc.
        sendSingleMessageToMesh(senderNick, chunk, part * PART_DELAY_MS);

        offset += chunkLen;
    }
}

void BitchatBridge::relayDirectMessageToMesh(const BitchatMessage& msg, const char* text) {
    // For DMs, we need to find the recipient in Meshcore contacts
    // This requires integration with the contact database
    // For now, log and skip
    BITCHAT_DEBUG_PRINTLN("DM relay not yet implemented - need contact lookup");

    // TODO: Implement DM relay
    // 1. Map Bitchat recipient ID to Meshcore Identity
    // 2. Look up shared secret
    // 3. Create encrypted TXT_MSG packet
    // 4. Send via appropriate routing
}

// ============================================================================
// Fragment Reassembly
// ============================================================================

void BitchatBridge::handleFragment(const BitchatMessage& msg) {
    // Fragment header format (from Bitchat protocol):
    // [fragmentId:1][totalFragments:1][fragmentIndex:1][data...]
    if (msg.payloadLength < 3) {
        return;
    }

    uint8_t fragmentId = msg.payload[0];
    uint8_t totalFragments = msg.payload[1];
    uint8_t fragmentIndex = msg.payload[2];

    uint64_t senderId = msg.getSenderId64();

    // Validate fragment parameters
    if (totalFragments == 0 || totalFragments > 8 || fragmentIndex >= totalFragments) {
        return;
    }

    uint32_t now = millis();

    // Clean up expired fragment buffers
    for (size_t i = 0; i < MAX_FRAGMENT_BUFFERS; i++) {
        if (_fragmentBuffers[i].active &&
            (now - _fragmentBuffers[i].startTime) > FRAGMENT_TIMEOUT_MS) {
            _fragmentBuffers[i].active = false;
        }
    }

    // Find existing buffer for this sender/fragmentId
    FragmentBuffer* buf = nullptr;
    for (size_t i = 0; i < MAX_FRAGMENT_BUFFERS; i++) {
        if (_fragmentBuffers[i].active &&
            _fragmentBuffers[i].senderId == senderId &&
            _fragmentBuffers[i].fragmentId == fragmentId) {
            buf = &_fragmentBuffers[i];
            break;
        }
    }

    // New fragment sequence - find empty buffer
    if (buf == nullptr) {
        if (msg.type != BITCHAT_MSG_FRAGMENT_NEW && fragmentIndex != 0) {
            // Missed the first fragment - can't reassemble
            return;
        }

        for (size_t i = 0; i < MAX_FRAGMENT_BUFFERS; i++) {
            if (!_fragmentBuffers[i].active) {
                buf = &_fragmentBuffers[i];
                buf->active = true;
                buf->senderId = senderId;
                buf->fragmentId = fragmentId;
                buf->totalFragments = totalFragments;
                buf->receivedMask = 0;
                buf->dataLen = 0;
                buf->startTime = now;
                memset(buf->data, 0, sizeof(buf->data));
                break;
            }
        }
    }

    if (buf == nullptr) {
        return;
    }

    // Copy fragment data
    size_t dataOffset = 3;  // Skip header
    size_t dataLen = msg.payloadLength - dataOffset;

    // Each fragment contains ~240 bytes of data (245 - 3 header - 2 checksum)
    size_t fragmentDataSize = 240;
    size_t insertOffset = fragmentIndex * fragmentDataSize;

    if (insertOffset + dataLen > sizeof(buf->data)) {
        return;
    }

    memcpy(&buf->data[insertOffset], &msg.payload[dataOffset], dataLen);
    buf->receivedMask |= (1 << fragmentIndex);

    // Track total data length
    size_t endPos = insertOffset + dataLen;
    if (endPos > buf->dataLen) {
        buf->dataLen = endPos;
    }

    BITCHAT_DEBUG_PRINTLN("Fragment %d/%d stored", fragmentIndex + 1, totalFragments);

    // Check if complete
    uint8_t expectedMask = (1 << totalFragments) - 1;
    if (buf->receivedMask == expectedMask) {
        // Reassembly complete!
        BITCHAT_DEBUG_PRINTLN("Fragment reassembly complete (%zu bytes)", buf->dataLen);

        // Create synthetic MESSAGE from reassembled data
        BitchatMessage reassembled;
        reassembled.version = msg.version;
        reassembled.type = BITCHAT_MSG_MESSAGE;
        reassembled.ttl = msg.ttl;
        reassembled.timestamp = msg.timestamp;
        reassembled.flags = msg.flags;
        memcpy(reassembled.senderId, msg.senderId, 8);
        memcpy(reassembled.recipientId, msg.recipientId, 8);

        // Copy reassembled data to payload
        // Note: For very long messages, we may need to split into multiple mesh messages
        size_t copyLen = buf->dataLen;
        if (copyLen > BITCHAT_MAX_PAYLOAD_SIZE) {
            copyLen = BITCHAT_MAX_PAYLOAD_SIZE;
        }
        memcpy(reassembled.payload, buf->data, copyLen);
        reassembled.payloadLength = static_cast<uint16_t>(copyLen);

        // Release buffer before processing (in case processing takes time)
        buf->active = false;

        // Process the reassembled message
        processBitchatMessage(reassembled);
    }
}

// ============================================================================
// Meshcore → Bitchat
// ============================================================================

void BitchatBridge::onMeshcoreGroupMessage(const mesh::GroupChannel& channel, uint32_t timestamp,
                                            const char* senderName, const char* text) {
#ifdef ESP32
    // IMPORTANT: Only relay #mesh channel messages to Bitchat
    if (!isMeshChannel(channel)) {
        // Not the #mesh channel - don't relay
        return;
    }

    // Check if this message originated from Bitchat (has phone emoji prefix)
    // to prevent rebroadcast loops. UTF-8 phone emoji (📱) is 4 bytes: 0xF0 0x9F 0x93 0xB1
    if (text != nullptr && strlen(text) >= 4) {
        if ((uint8_t)text[0] == 0xF0 && (uint8_t)text[1] == 0x9F &&
            (uint8_t)text[2] == 0x93 && (uint8_t)text[3] == 0xB1) {
            BITCHAT_DEBUG_PRINTLN("Skipping relay - message originated from Bitchat");
            return;
        }
    }

    // Build simple message content: "<senderName> text"
    // Bitchat displays MESSAGE payload as plain text
    char fullContent[200];
    snprintf(fullContent, sizeof(fullContent), "<%s> %s", senderName, text);

    // Create Bitchat message
    BitchatMessage msg;
    msg.version = BITCHAT_VERSION;
    msg.type = BITCHAT_MSG_MESSAGE;
    msg.ttl = DEFAULT_TTL;
    msg.timestamp = getCurrentTimeMs();
    msg.flags = 0;  // No special flags - simple channel message
    msg.setSenderId64(_bitchatPeerId);

    // Simple payload format - just copy the text content directly
    size_t contentLen = strlen(fullContent);
    if (contentLen > BITCHAT_MAX_PAYLOAD_SIZE) {
        contentLen = BITCHAT_MAX_PAYLOAD_SIZE;
    }
    memcpy(msg.payload, fullContent, contentLen);
    msg.payloadLength = static_cast<uint16_t>(contentLen);

    // Sign the message
    signMessage(msg);

    // Add to message history for REQUEST_SYNC responses
    addToMessageHistory(msg);

    _bleService.broadcastMessage(msg);
    BITCHAT_DEBUG_PRINTLN("TX to Bitchat: %s", senderName);
#endif
}

void BitchatBridge::onMeshcoreDirectMessage(const uint8_t* senderPubKey, uint32_t timestamp, const char* text) {
#ifdef ESP32
    if (!_bleService.hasConnectedClient()) {
        return;
    }

    // Derive sender's Bitchat ID from their public key
    uint64_t senderId = 0;
    for (int i = 0; i < 8; i++) {
        senderId |= (static_cast<uint64_t>(senderPubKey[i]) << (i * 8));
    }

    // Create Bitchat DM
    BitchatMessage msg;
    msg.version = BITCHAT_VERSION;
    msg.type = BITCHAT_MSG_MESSAGE;
    msg.ttl = DEFAULT_TTL;
    msg.timestamp = static_cast<uint64_t>(timestamp) * 1000ULL;
    msg.flags = BITCHAT_FLAG_HAS_RECIPIENT;
    msg.setSenderId64(senderId);
    msg.setRecipientId64(_bitchatPeerId);  // Recipient is us (relaying to BLE client)

    size_t textLen = strlen(text);
    if (textLen > BITCHAT_MAX_PAYLOAD_SIZE) textLen = BITCHAT_MAX_PAYLOAD_SIZE;
    memcpy(msg.payload, text, textLen);
    msg.payloadLength = static_cast<uint16_t>(textLen);

    _bleService.broadcastMessage(msg);
    BITCHAT_DEBUG_PRINTLN("Sent DM to Bitchat from %llX", senderId);
#endif
}

void BitchatBridge::onMeshcoreAdvert(const mesh::Identity& id, uint32_t timestamp,
                                      const uint8_t* appData, size_t appDataLen) {
#ifdef ESP32
    if (!_bleService.hasConnectedClient()) {
        return;
    }

    // Convert Meshcore advert to Bitchat announce
    uint64_t peerId = 0;
    for (int i = 0; i < 8; i++) {
        peerId |= (static_cast<uint64_t>(id.pub_key[i]) << (i * 8));
    }

    // Extract name from app data if available
    const char* name = "Unknown";
    if (appData != nullptr && appDataLen > 0) {
        // Meshcore advert app_data often contains the node name
        // This depends on how the advert was created
        name = reinterpret_cast<const char*>(appData);
    }

    // Derive Curve25519 key from the peer's Ed25519 key
    uint8_t peerNoiseKey[32];
    deriveNoisePublicKey(id.pub_key, peerNoiseKey);

    BitchatMessage msg;
    BitchatProtocol::createAnnounce(
        msg,
        peerId,
        name,
        peerNoiseKey,         // Curve25519 for Noise protocol
        id.pub_key,           // Ed25519 for signatures
        static_cast<uint64_t>(timestamp) * 1000ULL,
        DEFAULT_TTL
    );

    _bleService.broadcastMessage(msg);
    BITCHAT_DEBUG_PRINTLN("Sent Meshcore advert to Bitchat: %llX", peerId);
#endif
}

void BitchatBridge::broadcastToBitchat(const BitchatMessage& msg) {
#ifdef ESP32
    _bleService.broadcastMessage(msg);
#endif
}
