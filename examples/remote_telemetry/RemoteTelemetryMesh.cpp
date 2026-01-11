#include "RemoteTelemetryMesh.h"

#include <Arduino.h>
#include <string.h>

#include "RemoteTelemetryManager.h"

#ifndef REMOTE_TELEMETRY_DEBUG
#define REMOTE_TELEMETRY_DEBUG 1
#endif

#ifndef REMOTE_TELEMETRY_NODE_NAME
#define REMOTE_TELEMETRY_NODE_NAME "Remote Telemetry"
#endif

#if REMOTE_TELEMETRY_DEBUG
#define RT_DEBUG_PRINTLN(F, ...) Serial.printf("[mesh] " F "\n", ##__VA_ARGS__)
#else
#define RT_DEBUG_PRINTLN(...) do { } while (0)
#endif

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250

RemoteTelemetryMesh::RemoteTelemetryMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
  : BaseChatMesh(radio,
                 *(_clock = new ArduinoMillis()),
                 rng,
                 rtc,
                 *(_pktManager = new StaticPoolPacketManager(16)),
                 tables),
    _manager(nullptr) {
}

RemoteTelemetryMesh::~RemoteTelemetryMesh() {
}

void RemoteTelemetryMesh::begin() {
  Mesh::begin();
}

void RemoteTelemetryMesh::sendSelfAdvertisement(int delay_millis) {
  const char* name = REMOTE_TELEMETRY_NODE_NAME;
  if (!name || name[0] == '\0') {
    name = "Remote Telemetry";
  }

  mesh::Packet* pkt = createSelfAdvert(name);
  if (pkt) {
    sendFlood(pkt, delay_millis);
  } else {
    RT_DEBUG_PRINTLN("Failed to create self advertisement packet");
  }
}

void RemoteTelemetryMesh::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  RT_DEBUG_PRINTLN("Discovered contact %s (is_new=%d, path_len=%d)", contact.name, is_new ? 1 : 0, path_len);
  if (is_new && path_len > 0 && path_len <= MAX_PATH_SIZE) {
    contact.out_path_len = static_cast<int8_t>(path_len);
    memcpy(contact.out_path, path, path_len);
  }
}

ContactInfo* RemoteTelemetryMesh::processAck(const uint8_t* /*data*/) {
  return nullptr;
}

void RemoteTelemetryMesh::onContactPathUpdated(const ContactInfo& contact) {
  RT_DEBUG_PRINTLN("Path updated for %s (len=%d)", contact.name, contact.out_path_len);
}

bool RemoteTelemetryMesh::onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  return BaseChatMesh::onContactPathRecv(from, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void RemoteTelemetryMesh::onMessageRecv(const ContactInfo& contact, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const char* /*text*/) {
  RT_DEBUG_PRINTLN("Ignoring text message from %s", contact.name);
}

void RemoteTelemetryMesh::onCommandDataRecv(const ContactInfo& contact, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const char* /*text*/) {
  RT_DEBUG_PRINTLN("Ignoring command data from %s", contact.name);
}

void RemoteTelemetryMesh::onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* /*pkt*/, uint32_t /*sender_timestamp*/, const uint8_t* /*sender_prefix*/, const char* /*text*/) {
  RT_DEBUG_PRINTLN("Ignoring signed message from %s", contact.name);
}

void RemoteTelemetryMesh::onChannelMessageRecv(const mesh::GroupChannel& /*channel*/, mesh::Packet* /*pkt*/, uint32_t /*timestamp*/, const char* /*text*/) {
  RT_DEBUG_PRINTLN("Ignoring channel message");
}

uint32_t RemoteTelemetryMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}

uint32_t RemoteTelemetryMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  return SEND_TIMEOUT_BASE_MILLIS + ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_len + 1));
}

void RemoteTelemetryMesh::onSendTimeout() {
  if (_manager) {
    _manager->notifySendTimeout();
  }
}

uint8_t RemoteTelemetryMesh::onContactRequest(const ContactInfo& /*contact*/, uint32_t /*sender_timestamp*/, const uint8_t* /*data*/, uint8_t /*len*/, uint8_t* /*reply*/) {
  return 0;
}

void RemoteTelemetryMesh::onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) {
  if (!_manager || len < 5) {
    return;
  }

  if ((len >= 6 && memcmp(&data[4], "OK", 2) == 0) || data[4] == REMOTE_RESP_SERVER_LOGIN_OK) {
    _manager->handleLoginResponse(contact, data, len);
    return;
  }

  uint32_t tag;
  memcpy(&tag, data, sizeof(tag));
  _manager->handleTelemetryResponse(contact, tag, &data[4], len - 4);
}
