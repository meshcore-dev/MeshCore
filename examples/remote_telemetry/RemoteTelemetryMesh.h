#pragma once

#include <helpers/BaseChatMesh.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/ArduinoHelpers.h>

class RemoteTelemetryManager;

constexpr uint8_t REMOTE_RESP_SERVER_LOGIN_OK = 0;

class RemoteTelemetryMesh : public BaseChatMesh {
public:
  RemoteTelemetryMesh(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);
  ~RemoteTelemetryMesh();

  void begin();
  void sendSelfAdvertisement(int delay_millis);
  void setManager(RemoteTelemetryManager* manager) { _manager = manager; }

protected:
  bool isAutoAddEnabled() const override { return false; }
  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  ContactInfo* processAck(const uint8_t* data) override;
  void onContactPathUpdated(const ContactInfo& contact) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override;
  void onCommandDataRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override;
  void onSignedMessageRecv(const ContactInfo& contact, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t* sender_prefix, const char* text) override;
  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char* text) override;
  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;
  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override;
  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override;

private:
  RemoteTelemetryManager* _manager;
  StaticPoolPacketManager* _pktManager;
  ArduinoMillis* _clock;
};
