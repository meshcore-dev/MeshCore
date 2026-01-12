#include "ChatBotMesh.h"

#include "ChatBotManager.h"

#include <Utils.h>
#include <cstring>

namespace {
constexpr uint32_t SEND_TIMEOUT_BASE_MILLIS = 500;
constexpr float FLOOD_TIMEOUT_FACTOR = 16.0f;
constexpr float DIRECT_TIMEOUT_FACTOR = 6.0f;
constexpr uint32_t DIRECT_TIMEOUT_EXTRA = 250;
}

ChatBotMesh::ChatBotMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
  : BaseChatMesh(radio,
                 *(_clock = new ArduinoMillis()),
                 rng,
                 rtc,
                 *(_packetManager = new StaticPoolPacketManager(16)),
                 tables),
    _manager(nullptr),
    _channelDetails(nullptr) {
}

void ChatBotMesh::begin() {
  Mesh::begin();
}

void ChatBotMesh::setManager(ChatBotManager* manager) {
  _manager = manager;
}

String ChatBotMesh::toBase64(const uint8_t* data, size_t len) {
  static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!data || len == 0) {
    return String();
  }
  String encoded;
  encoded.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;
    size_t remaining = len - i;
    if (remaining > 1) {
      chunk |= static_cast<uint32_t>(data[i + 1]) << 8;
    }
    if (remaining > 2) {
      chunk |= static_cast<uint32_t>(data[i + 2]);
    }
    encoded += ALPHABET[(chunk >> 18) & 0x3F];
    encoded += ALPHABET[(chunk >> 12) & 0x3F];
    encoded += (remaining > 1) ? ALPHABET[(chunk >> 6) & 0x3F] : '=';
    encoded += (remaining > 2) ? ALPHABET[chunk & 0x3F] : '=';
  }
  return encoded;
}

bool ChatBotMesh::applyChannelSecret(const std::vector<uint8_t>& keyBytes) {
  if (!_channelDetails) {
    return false;
  }
  if (keyBytes.size() != 16 && keyBytes.size() != 32) {
    return false;
  }
  memset(_channelDetails->channel.secret, 0, sizeof(_channelDetails->channel.secret));
  memcpy(_channelDetails->channel.secret, keyBytes.data(), keyBytes.size());
  mesh::Utils::sha256(_channelDetails->channel.hash,
                      sizeof(_channelDetails->channel.hash),
                      _channelDetails->channel.secret,
                      keyBytes.size());
  return true;
}

bool ChatBotMesh::configureChannel(const String& name, const String& keyHex) {
  String sanitized = chatbot::normalizeChannelKey(keyHex);
  if (!chatbot::isChannelKeyValid(sanitized)) {
    return false;
  }

  std::vector<uint8_t> keyBytes;
  if (!chatbot::decodeHexKey(sanitized.c_str(), keyBytes)) {
    return false;
  }

  if (!_channelDetails) {
    String base64 = toBase64(keyBytes.data(), keyBytes.size());
    ChannelDetails* details = addChannel(name.c_str(), base64.c_str());
    if (!details) {
      return false;
    }
    _channelDetails = details;
  }

  StrHelper::strncpy(_channelDetails->name, name.c_str(), sizeof(_channelDetails->name));
  return applyChannelSecret(keyBytes);
}

bool ChatBotMesh::sendChannelMessage(const String& senderName, const String& text) {
  if (!_channelDetails) {
    return false;
  }
  String trimmedText = text;
  trimmedText.trim();
  if (trimmedText.length() == 0) {
    return false;
  }

  String name = senderName;
  name.trim();
  if (name.length() == 0) {
    name = "MeshBot";
  }

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  return sendGroupMessage(now,
                          _channelDetails->channel,
                          name.c_str(),
                          trimmedText.c_str(),
                          static_cast<int>(trimmedText.length()));
}

void ChatBotMesh::onDiscoveredContact(ContactInfo& /*contact*/, bool /*is_new*/, uint8_t /*path_len*/, const uint8_t* /*path*/) {
}

ContactInfo* ChatBotMesh::processAck(const uint8_t* /*data*/) {
  return nullptr;
}

void ChatBotMesh::onContactPathUpdated(const ContactInfo& /*contact*/) {
}

bool ChatBotMesh::onContactPathRecv(ContactInfo& from,
                                    uint8_t* in_path,
                                    uint8_t in_path_len,
                                    uint8_t* out_path,
                                    uint8_t out_path_len,
                                    uint8_t extra_type,
                                    uint8_t* extra,
                                    uint8_t extra_len) {
  return BaseChatMesh::onContactPathRecv(from, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void ChatBotMesh::onMessageRecv(const ContactInfo& contact, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const char* /*text*/) {
  Serial.printf("[chatbot] Ignoring direct message from %s\n", contact.name);
}

void ChatBotMesh::onCommandDataRecv(const ContactInfo& contact, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const char* /*text*/) {
  Serial.printf("[chatbot] Ignoring command data from %s\n", contact.name);
}

void ChatBotMesh::onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const uint8_t* /*sender_prefix*/, const char* /*text*/) {
  Serial.printf("[chatbot] Ignoring signed message from %s\n", contact.name);
}

void ChatBotMesh::onChannelMessageRecv(const mesh::GroupChannel& /*channel*/, mesh::Packet* pkt, uint32_t timestamp, const char* text) {
  if (_manager) {
    bool direct = pkt ? pkt->isRouteDirect() : false;
    _manager->handleMeshChannelMessage(timestamp, text, direct);
  }
}

uint32_t ChatBotMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + static_cast<uint32_t>(FLOOD_TIMEOUT_FACTOR * pkt_airtime_millis);
}

uint32_t ChatBotMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  return SEND_TIMEOUT_BASE_MILLIS +
         static_cast<uint32_t>((pkt_airtime_millis * DIRECT_TIMEOUT_FACTOR + DIRECT_TIMEOUT_EXTRA) * (path_len + 1));
}

void ChatBotMesh::onSendTimeout() {
  if (_manager) {
    _manager->notifySendTimeout();
  }
}

uint8_t ChatBotMesh::onContactRequest(const ContactInfo& /*contact*/, uint32_t /*sender_timestamp*/, const uint8_t* /*data*/, uint8_t /*len*/, uint8_t* /*reply*/) {
  return 0;
}

void ChatBotMesh::onContactResponse(const ContactInfo& /*contact*/, const uint8_t* /*data*/, uint8_t /*len*/) {
}
