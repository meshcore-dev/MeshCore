#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include <gtest/gtest.h>

#include "helpers/BaseChatMesh.h"
#include "helpers/SimpleMeshTables.h"

namespace {

constexpr char kSenderPrivateKeyHex[] =
    "70"
    "65e18fd9fabb70c1ed90dca19907de698c88b709ea146eafd93d9b830c7b60"
    "c4681193c79bbc39945ba8064104bb618f8fd7a84a0af6f57033d6e8ddcd6471";
constexpr char kSenderPublicKeyHex[] =
    "1ec77175b0918ed206f9ae04ec136d6d5d4315bb26305427f645b492e9350c10";

void WriteU8(uint8_t* dest, size_t* offset, uint8_t value) {
  dest[(*offset)++] = value;
}

void WriteI32Le(uint8_t* dest, size_t* offset, int32_t value) {
  const uint32_t raw = static_cast<uint32_t>(value);
  dest[(*offset)++] = static_cast<uint8_t>(raw & 0xFF);
  dest[(*offset)++] = static_cast<uint8_t>((raw >> 8) & 0xFF);
  dest[(*offset)++] = static_cast<uint8_t>((raw >> 16) & 0xFF);
  dest[(*offset)++] = static_cast<uint8_t>((raw >> 24) & 0xFF);
}

void WriteBytes(uint8_t* dest, size_t* offset, const uint8_t* bytes, size_t length) {
  memcpy(dest + *offset, bytes, length);
  *offset += length;
}

template <size_t N>
void WriteStringLiteral(uint8_t* dest, size_t* offset, const char (&value)[N]) {
  static_assert(N > 0, "string literal must include a null terminator");
  WriteBytes(dest, offset, reinterpret_cast<const uint8_t*>(value), N - 1);
}

class FakeMillis final : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override {
    return 0;
  }
};

class FakeRtc final : public mesh::RTCClock {
public:
  explicit FakeRtc(uint32_t initial_time) : _time(initial_time) {}

  uint32_t getCurrentTime() override {
    return _time;
  }

  void setCurrentTime(uint32_t time) override {
    _time = time;
  }

private:
  uint32_t _time;
};

class FakeRng final : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override {
    memset(dest, 0x5A, sz);
  }
};

class FakeRadio final : public mesh::Radio {
public:
  int recvRaw(uint8_t*, int) override {
    return 0;
  }

  uint32_t getEstAirtimeFor(int) override {
    return 1;
  }

  float packetScore(float, int) override {
    return 1.0f;
  }

  bool startSendRaw(const uint8_t*, int) override {
    return true;
  }

  bool isSendComplete() override {
    return true;
  }

  void onSendFinished() override {}

  bool isInRecvMode() const override {
    return true;
  }
};

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

mesh::Packet BuildSignedAdvertPacket(uint32_t timestamp, const uint8_t* app_data, uint8_t app_data_len) {
  mesh::LocalIdentity sender(kSenderPrivateKeyHex, kSenderPublicKeyHex);
  mesh::Packet packet;

  // Wire header: flood-routed advert packet with no path hashes yet.
  packet.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  packet.path_len = 0;

  int offset = 0;
  // Sender public key: used by the receiver to identify who signed the advert.
  memcpy(&packet.payload[offset], sender.pub_key, PUB_KEY_SIZE);
  offset += PUB_KEY_SIZE;

  // Advert timestamp: the sender's monotonic advert time used for replay checks.
  memcpy(&packet.payload[offset], &timestamp, sizeof(timestamp));
  offset += sizeof(timestamp);

  // Signature field: filled after the signed message bytes are assembled below.
  uint8_t* signature = &packet.payload[offset];
  offset += SIGNATURE_SIZE;

  // Raw advert app_data: arbitrary bytes authored by the test, not by createAdvert().
  memcpy(&packet.payload[offset], app_data, app_data_len);
  offset += app_data_len;
  packet.payload_len = offset;

  uint8_t message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE];
  int message_len = 0;
  memcpy(&message[message_len], sender.pub_key, PUB_KEY_SIZE);
  message_len += PUB_KEY_SIZE;
  memcpy(&message[message_len], &timestamp, sizeof(timestamp));
  message_len += sizeof(timestamp);
  memcpy(&message[message_len], app_data, app_data_len);
  message_len += app_data_len;

  sender.sign(signature, message, message_len);
  return packet;
}

TEST(AdvertData, ParsesNameOnlyFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: chat advert with a trailing name field.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_NAME_MASK);
  // name field: raw bytes for "alice", consuming the rest of app_data.
  WriteStringLiteral(app_data, &offset, "alice");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  ASSERT_TRUE(mesh.discovered_contact.has_value());
  EXPECT_EQ(ADV_TYPE_CHAT, mesh.discovered_contact->type);
  EXPECT_STREQ("alice", mesh.discovered_contact->name);
  EXPECT_EQ(advert_timestamp, mesh.discovered_contact->last_advert_timestamp);
  EXPECT_EQ(1704067200U, mesh.discovered_contact->lastmod);
  EXPECT_EQ(0, mesh.discovered_contact->gps_lat);
  EXPECT_EQ(0, mesh.discovered_contact->gps_lon);
}

TEST(AdvertData, ParsesNameAndCoordinatesFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: repeater advert with lat/lon followed by a name.
  WriteU8(app_data, &offset, ADV_TYPE_REPEATER | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: signed little-endian microdegrees for 37.7749.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: signed little-endian microdegrees for -122.4194.
  WriteI32Le(app_data, &offset, -122419400);
  // name field: raw bytes for "node" after the coordinate fields.
  WriteStringLiteral(app_data, &offset, "node");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  ASSERT_TRUE(mesh.discovered_contact.has_value());
  EXPECT_EQ(ADV_TYPE_REPEATER, mesh.discovered_contact->type);
  EXPECT_STREQ("node", mesh.discovered_contact->name);
  EXPECT_EQ(37774900, mesh.discovered_contact->gps_lat);
  EXPECT_EQ(-122419400, mesh.discovered_contact->gps_lon);
}

TEST(AdvertData, ParsesCoordinateExtremesFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: sensor advert with both location fields and a name.
  WriteU8(app_data, &offset, ADV_TYPE_SENSOR | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: minimum supported latitude, -90.000000 degrees.
  WriteI32Le(app_data, &offset, -90000000);
  // longitude field: maximum supported longitude, 180.000000 degrees.
  WriteI32Le(app_data, &offset, 180000000);
  // name field: raw bytes for "edge".
  WriteStringLiteral(app_data, &offset, "edge");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  ASSERT_TRUE(mesh.discovered_contact.has_value());
  EXPECT_EQ(ADV_TYPE_SENSOR, mesh.discovered_contact->type);
  EXPECT_STREQ("edge", mesh.discovered_contact->name);
  EXPECT_EQ(-90000000, mesh.discovered_contact->gps_lat);
  EXPECT_EQ(180000000, mesh.discovered_contact->gps_lon);
}

TEST(AdvertData, RejectsLongitudeOutsideValidRangeFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: valid latitude so the failure comes from longitude.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: one microdegree above +180.0, which is invalid.
  WriteI32Le(app_data, &offset, 180000001);
  // name field: parser should reject before the trailing name matters.
  WriteStringLiteral(app_data, &offset, "node");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  EXPECT_FALSE(mesh.discovered_contact.has_value());
}

TEST(AdvertData, RejectsLongitudeBelowValidRangeFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: valid latitude so the failure comes from longitude.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: one microdegree below -180.0, which is invalid.
  WriteI32Le(app_data, &offset, -180000001);
  // name field: included to keep the payload shape consistent.
  WriteStringLiteral(app_data, &offset, "node");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  EXPECT_FALSE(mesh.discovered_contact.has_value());
}

TEST(AdvertData, RejectsLatitudeOutsideValidRangeFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: one microdegree above +90.0, which is invalid.
  WriteI32Le(app_data, &offset, 90000001);
  // longitude field: valid longitude so the failure comes from latitude.
  WriteI32Le(app_data, &offset, -122419400);
  // name field: included to keep the payload shape consistent.
  WriteStringLiteral(app_data, &offset, "node");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  EXPECT_FALSE(mesh.discovered_contact.has_value());
}

TEST(AdvertData, RejectsLatitudeBelowValidRangeFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: one microdegree below -90.0, which is invalid.
  WriteI32Le(app_data, &offset, -90000001);
  // longitude field: valid longitude so the failure comes from latitude.
  WriteI32Le(app_data, &offset, -122419400);
  // name field: included to keep the payload shape consistent.
  WriteStringLiteral(app_data, &offset, "node");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  mesh.recv(&packet);

  EXPECT_FALSE(mesh.discovered_contact.has_value());
}

TEST(AdvertData, RejectsForgedSignatureFromNetworkPacket) {
  FakeRadio radio;
  FakeMillis ms;
  FakeRng rng;
  FakeRtc rtc(1704067200U);
  NoopPacketManager packet_manager;
  SimpleMeshTables tables;
  TestChatMesh mesh(radio, ms, rng, rtc, packet_manager, tables);

  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;
  constexpr uint32_t advert_timestamp = 1704067201U;

  // flags/type byte: chat advert with a trailing name field.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_NAME_MASK);
  // name field: raw bytes for "mallory".
  WriteStringLiteral(app_data, &offset, "mallory");

  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  // Corrupt the signature bytes after signing so verification must fail in Mesh::onRecvPacket().
  packet.payload[PUB_KEY_SIZE + 4] ^= 0xFF;

  mesh.recv(&packet);

  EXPECT_FALSE(mesh.discovered_contact.has_value());
}

}  // namespace
