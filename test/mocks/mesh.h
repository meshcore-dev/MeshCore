#pragma once

#include <cstdint>
#include <optional>

#include "helpers/BaseChatMesh.h"
#include "helpers/SimpleMeshTables.h"
#include "radio.h"
#include "random.h"
#include "time.h"

// No-op packet manager for native tests.
// Satisfies Mesh dependencies while preventing packet allocation or queuing.
class NoopPacketManager final : public mesh::PacketManager {
public:
  mesh::Packet* allocNew() override {
    return nullptr;
  }

  void free(mesh::Packet*) override {}

  void queueOutbound(mesh::Packet*, uint8_t, uint32_t) override {}

  mesh::Packet* getNextOutbound(uint32_t) override {
    return nullptr;
  }

  int getOutboundCount(uint32_t) const override {
    return 0;
  }

  int getOutboundTotal() const override {
    return 0;
  }

  int getFreeCount() const override {
    return 0;
  }

  mesh::Packet* getOutboundByIdx(int) override {
    return nullptr;
  }

  mesh::Packet* removeOutboundByIdx(int) override {
    return nullptr;
  }

  void queueInbound(mesh::Packet*, uint32_t) override {}

  mesh::Packet* getNextInbound(uint32_t) override {
    return nullptr;
  }
};

// Test chat mesh for native tests.
// Exposes packet receive handling and captures discovered contacts for assertions.
class TestChatMesh final : public BaseChatMesh {
public:
  TestChatMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
               mesh::PacketManager& mgr, mesh::MeshTables& tables)
      : BaseChatMesh(radio, ms, rng, rtc, mgr, tables) {}

  mesh::DispatcherAction recv(mesh::Packet* pkt) {
    return onRecvPacket(pkt);
  }

  std::optional<ContactInfo> discovered_contact;

protected:
  void onDiscoveredContact(ContactInfo& contact, bool, uint8_t, const uint8_t*) override {
    discovered_contact = contact;
  }

  ContactInfo* processAck(const uint8_t*) override {
    return nullptr;
  }

  void onContactPathUpdated(const ContactInfo&) override {}

  void onMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}

  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}

  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*, const char*) override {}

  uint32_t calcFloodTimeoutMillisFor(uint32_t) const override {
    return 0;
  }

  uint32_t calcDirectTimeoutMillisFor(uint32_t, uint8_t) const override {
    return 0;
  }

  void onSendTimeout() override {}

  void onChannelMessageRecv(const mesh::GroupChannel&, mesh::Packet*, uint32_t, const char*) override {}

  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t, uint8_t*) override {
    return 0;
  }

  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}
};

// Test mesh context for native tests.
// Owns mock dependencies in construction order and exposes the mesh via operator->.
struct TestMeshContext {
  explicit TestMeshContext(uint32_t current_timestamp)
      : rtc(current_timestamp), mesh(radio, ms, rng, rtc, packet_manager, tables) {}

  TestChatMesh* operator->() {
    return &mesh;
  }

  const TestChatMesh* operator->() const {
    return &mesh;
  }

  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc;
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh;
};
