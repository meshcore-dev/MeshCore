#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "mocks.h"

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

TestMeshContext MakeTestMesh(uint32_t current_timestamp) {
  return TestMeshContext(current_timestamp);
}

ContactInfo MakeSenderContact(uint32_t advert_timestamp, int32_t gps_lat, int32_t gps_lon) {
  ContactInfo contact = {};
  contact.id = mesh::Identity(kSenderPublicKeyHex);
  strcpy(contact.name, "existing-contact");
  contact.type = ADV_TYPE_CHAT;
  contact.out_path_len = OUT_PATH_UNKNOWN;
  contact.last_advert_timestamp = advert_timestamp;
  contact.lastmod = advert_timestamp;
  contact.gps_lat = gps_lat;
  contact.gps_lon = gps_lon;
  return contact;
}

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

TEST(AdvertData, ParsesNameAndCoordinatesFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: repeater advert with lat/lon followed by a name.
  WriteU8(app_data, &offset, ADV_TYPE_REPEATER | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: signed little-endian microdegrees for 37.7749.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: signed little-endian microdegrees for -122.4194.
  WriteI32Le(app_data, &offset, -122419400);
  // name field: trailing contact name bytes after the coordinate fields.
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  ASSERT_TRUE(test_mesh->discovered_contact.has_value());
  EXPECT_EQ(ADV_TYPE_REPEATER, test_mesh->discovered_contact->type);
  EXPECT_STREQ("dummy-node-name", test_mesh->discovered_contact->name);
  EXPECT_EQ(37774900, test_mesh->discovered_contact->gps_lat);
  EXPECT_EQ(-122419400, test_mesh->discovered_contact->gps_lon);
}

TEST(AdvertData, ParsesCoordinateExtremesFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: sensor advert with both location fields and a name.
  WriteU8(app_data, &offset, ADV_TYPE_SENSOR | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: minimum supported latitude, -90.000000 degrees.
  WriteI32Le(app_data, &offset, -90000000);
  // longitude field: maximum supported longitude, 180.000000 degrees.
  WriteI32Le(app_data, &offset, 180000000);
  // name field: raw bytes for "dummy-node-name".
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  ASSERT_TRUE(test_mesh->discovered_contact.has_value());
  EXPECT_EQ(ADV_TYPE_SENSOR, test_mesh->discovered_contact->type);
  EXPECT_STREQ("dummy-node-name", test_mesh->discovered_contact->name);
  EXPECT_EQ(-90000000, test_mesh->discovered_contact->gps_lat);
  EXPECT_EQ(180000000, test_mesh->discovered_contact->gps_lon);
}

TEST(AdvertData, ParsesPositiveLatitudeAndNegativeLongitudeBoundariesFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: sensor advert with both location fields and a name.
  WriteU8(app_data, &offset, ADV_TYPE_SENSOR | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: maximum supported latitude, +90.000000 degrees.
  WriteI32Le(app_data, &offset, 90000000);
  // longitude field: minimum supported longitude, -180.000000 degrees.
  WriteI32Le(app_data, &offset, -180000000);
  // name field: raw bytes for "dummy-node-name".
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  ASSERT_TRUE(test_mesh->discovered_contact.has_value());
  EXPECT_EQ(90000000, test_mesh->discovered_contact->gps_lat);
  EXPECT_EQ(-180000000, test_mesh->discovered_contact->gps_lon);
}

TEST(AdvertData, ParsesNullIslandCoordinatesFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with zero-valued coordinates and a name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: Null Island latitude at exactly 0.000000 degrees.
  WriteI32Le(app_data, &offset, 0);
  // longitude field: Null Island longitude at exactly 0.000000 degrees.
  WriteI32Le(app_data, &offset, 0);
  // name field: raw bytes for "dummy-node-name".
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  ASSERT_TRUE(test_mesh->discovered_contact.has_value());
  EXPECT_EQ(0, test_mesh->discovered_contact->gps_lat);
  EXPECT_EQ(0, test_mesh->discovered_contact->gps_lon);
}

TEST(AdvertData, RejectsLongitudeOutsideValidRangeFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: valid latitude so the failure comes from longitude.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: one microdegree above +180.0, which is invalid.
  WriteI32Le(app_data, &offset, 180000001);
  // name field: parser should reject before the trailing name matters.
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsLongitudeBelowValidRangeFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: valid latitude so the failure comes from longitude.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: one microdegree below -180.0, which is invalid.
  WriteI32Le(app_data, &offset, -180000001);
  // name field: included to keep the payload shape consistent.
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsGpsPayloadWithMissingFlagsByte) {
  // Backing storage is unused because the advertised app_data length is zero.
  uint8_t app_data[1] = {};
  size_t offset = 0;

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  // Leave the app_data length at zero so the parser never sees the flags byte.
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsGpsPayloadWithOnlyFlagsByte) {
  uint8_t app_data[1] = {};
  size_t offset = 0;

  // flags/type byte: chat advert that claims to carry coordinates and a name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  // Pass only the flags byte so no latitude bytes remain.
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsGpsPayloadWithLatitudeButMissingLongitude) {
  uint8_t app_data[5] = {};
  size_t offset = 0;

  // flags/type byte: chat advert that claims to carry coordinates and a name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: complete latitude bytes are present before truncation.
  WriteI32Le(app_data, &offset, 37774900);

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  // Pass only the flags byte and latitude field so longitude is missing.
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsGpsPayloadOneByteShortOfFullCoordinates) {
  uint8_t app_data[8] = {};
  size_t offset = 0;
  uint8_t lon_bytes[sizeof(int32_t)] = {};
  size_t lon_offset = 0;

  // flags/type byte: chat advert that claims to carry coordinates and a name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: complete latitude bytes are present before truncation.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: only the first three longitude bytes are present before truncation.
  WriteI32Le(lon_bytes, &lon_offset, -122419400);
  WriteBytes(app_data, &offset, lon_bytes, 3);

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  // Pass only the flags byte, latitude field, and three longitude bytes.
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsLatitudeOutsideValidRangeFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: one microdegree above +90.0, which is invalid.
  WriteI32Le(app_data, &offset, 90000001);
  // longitude field: valid longitude so the failure comes from latitude.
  WriteI32Le(app_data, &offset, -122419400);
  // name field: included to keep the payload shape consistent.
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, RejectsLatitudeBelowValidRangeFromNetworkPacket) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with location and name fields present.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: one microdegree below -90.0, which is invalid.
  WriteI32Le(app_data, &offset, -90000001);
  // longitude field: valid longitude so the failure comes from latitude.
  WriteI32Le(app_data, &offset, -122419400);
  // name field: included to keep the payload shape consistent.
  WriteStringLiteral(app_data, &offset, "dummy-node-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  test_mesh->recv(&packet);

  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

TEST(AdvertData, KeepsExistingGpsWhenUpdatedAdvertOmitsCoordinates) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with a new name but no GPS fields.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_NAME_MASK);
  // name field: replacement contact name with no coordinate payload following it.
  WriteStringLiteral(app_data, &offset, "updated-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t existing_advert_timestamp = current_timestamp - 10;
  constexpr uint32_t new_advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(new_advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  ASSERT_TRUE(test_mesh->addContact(MakeSenderContact(existing_advert_timestamp, 37774900, -122419400)));

  test_mesh->recv(&packet);

  ContactInfo* updated = test_mesh->lookupContactByPubKey(mesh::Identity(kSenderPublicKeyHex).pub_key, PUB_KEY_SIZE);
  ASSERT_NE(nullptr, updated);
  EXPECT_STREQ("updated-name", updated->name);
  EXPECT_EQ(37774900, updated->gps_lat);
  EXPECT_EQ(-122419400, updated->gps_lon);
  ASSERT_TRUE(test_mesh->discovered_contact.has_value());
  EXPECT_EQ(37774900, test_mesh->discovered_contact->gps_lat);
  EXPECT_EQ(-122419400, test_mesh->discovered_contact->gps_lon);
}

TEST(AdvertData, OverwritesExistingGpsWhenUpdatedAdvertIncludesCoordinates) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with replacement GPS coordinates and a new name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: replacement latitude for 40.712800 degrees.
  WriteI32Le(app_data, &offset, 40712800);
  // longitude field: replacement longitude for -74.006000 degrees.
  WriteI32Le(app_data, &offset, -74006000);
  // name field: replacement contact name applied with the new coordinates.
  WriteStringLiteral(app_data, &offset, "updated-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t existing_advert_timestamp = current_timestamp - 10;
  constexpr uint32_t new_advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(new_advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  ASSERT_TRUE(test_mesh->addContact(MakeSenderContact(existing_advert_timestamp, 37774900, -122419400)));

  test_mesh->recv(&packet);

  ContactInfo* updated = test_mesh->lookupContactByPubKey(mesh::Identity(kSenderPublicKeyHex).pub_key, PUB_KEY_SIZE);
  ASSERT_NE(nullptr, updated);
  EXPECT_STREQ("updated-name", updated->name);
  EXPECT_EQ(40712800, updated->gps_lat);
  EXPECT_EQ(-74006000, updated->gps_lon);
  ASSERT_TRUE(test_mesh->discovered_contact.has_value());
  EXPECT_EQ(40712800, test_mesh->discovered_contact->gps_lat);
  EXPECT_EQ(-74006000, test_mesh->discovered_contact->gps_lon);
}

TEST(AdvertData, LeavesExistingGpsUntouchedWhenUpdatedAdvertHasInvalidCoordinates) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with invalid longitude and a new name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: valid latitude so the update failure comes from longitude.
  WriteI32Le(app_data, &offset, 37774900);
  // longitude field: one microdegree above +180.0, which should reject the update.
  WriteI32Le(app_data, &offset, 180000001);
  // name field: replacement name that should not be applied when parsing fails.
  WriteStringLiteral(app_data, &offset, "updated-name");

  constexpr uint32_t current_timestamp = 1704067200U;
  constexpr uint32_t existing_advert_timestamp = current_timestamp - 10;
  constexpr uint32_t new_advert_timestamp = current_timestamp + 1;
  mesh::Packet packet = BuildSignedAdvertPacket(new_advert_timestamp, app_data, offset);

  auto test_mesh = MakeTestMesh(current_timestamp);
  ASSERT_TRUE(test_mesh->addContact(MakeSenderContact(existing_advert_timestamp, 37774900, -122419400)));

  test_mesh->recv(&packet);

  ContactInfo* existing = test_mesh->lookupContactByPubKey(mesh::Identity(kSenderPublicKeyHex).pub_key, PUB_KEY_SIZE);
  ASSERT_NE(nullptr, existing);
  EXPECT_STREQ("existing-contact", existing->name);
  EXPECT_EQ(37774900, existing->gps_lat);
  EXPECT_EQ(-122419400, existing->gps_lon);
  EXPECT_EQ(existing_advert_timestamp, existing->last_advert_timestamp);
  EXPECT_FALSE(test_mesh->discovered_contact.has_value());
}

}  // namespace
