#include <cstddef>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "helpers/AdvertDataHelpers.h"

namespace {

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

AdvertDataParser Parse(const uint8_t* app_data, size_t app_data_len) {
  return AdvertDataParser(app_data, static_cast<uint8_t>(app_data_len));
}

TEST(AdvertDataParser, ParsesNameAndCoordinates) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(ADV_TYPE_REPEATER, parser.getType());
  ASSERT_TRUE(parser.hasLatLon());
  EXPECT_EQ(37774900, parser.getIntLat());
  EXPECT_EQ(-122419400, parser.getIntLon());
  ASSERT_TRUE(parser.hasName());
  EXPECT_STREQ("dummy-node-name", parser.getName());
}

TEST(AdvertDataParser, ParsesCoordinateExtremes) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(ADV_TYPE_SENSOR, parser.getType());
  EXPECT_EQ(-90000000, parser.getIntLat());
  EXPECT_EQ(180000000, parser.getIntLon());
  EXPECT_STREQ("dummy-node-name", parser.getName());
}

TEST(AdvertDataParser, ParsesPositiveLatitudeAndNegativeLongitudeBoundaries) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(90000000, parser.getIntLat());
  EXPECT_EQ(-180000000, parser.getIntLon());
}

TEST(AdvertDataParser, ParsesNullIslandCoordinates) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(0, parser.getIntLat());
  EXPECT_EQ(0, parser.getIntLon());
}

TEST(AdvertDataParser, ParsesNameWithoutCoordinates) {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
  size_t offset = 0;

  // flags/type byte: chat advert with a name but no GPS fields.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_NAME_MASK);
  // name field: contact name with no coordinate payload before it.
  WriteStringLiteral(app_data, &offset, "updated-name");

  const AdvertDataParser parser = Parse(app_data, offset);

  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(ADV_TYPE_CHAT, parser.getType());
  EXPECT_FALSE(parser.hasLatLon());
  ASSERT_TRUE(parser.hasName());
  EXPECT_STREQ("updated-name", parser.getName());
}

TEST(AdvertDataParser, RejectsLongitudeOutsideValidRange) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsLongitudeBelowValidRange) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsLatitudeOutsideValidRange) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsLatitudeBelowValidRange) {
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

  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsPayloadWithMissingFlagsByte) {
  // Backing storage is unused because the advertised app_data length is zero.
  const uint8_t app_data[1] = {};

  const AdvertDataParser parser = Parse(app_data, 0);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsPayloadWithOnlyFlagsByte) {
  uint8_t app_data[1] = {};
  size_t offset = 0;

  // flags/type byte: chat advert that claims to carry coordinates and a name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);

  // Pass only the flags byte so no latitude bytes remain.
  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsPayloadWithLatitudeButMissingLongitude) {
  uint8_t app_data[5] = {};
  size_t offset = 0;

  // flags/type byte: chat advert that claims to carry coordinates and a name.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
  // latitude field: complete latitude bytes are present before truncation.
  WriteI32Le(app_data, &offset, 37774900);

  // Pass only the flags byte and latitude field so longitude is missing.
  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsPayloadOneByteShortOfFullCoordinates) {
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

  // Pass only the flags byte, latitude field, and three longitude bytes.
  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsPayloadWithIncompleteFeat1) {
  uint8_t app_data[2] = {};
  size_t offset = 0;

  // flags/type byte: chat advert that claims to carry a two-byte feature field.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_FEAT1_MASK);
  // feature field: only the first byte is present before truncation.
  WriteU8(app_data, &offset, 0x34);

  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, RejectsPayloadWithIncompleteFeat2) {
  uint8_t app_data[4] = {};
  size_t offset = 0;

  // flags/type byte: chat advert that claims to carry both two-byte feature fields.
  WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_FEAT1_MASK | ADV_FEAT2_MASK);
  // feature 1 field: complete two-byte value before the truncated feature 2 field.
  WriteU8(app_data, &offset, 0x34);
  WriteU8(app_data, &offset, 0x12);
  // feature 2 field: only the first byte is present before truncation.
  WriteU8(app_data, &offset, 0x78);

  const AdvertDataParser parser = Parse(app_data, offset);

  EXPECT_FALSE(parser.isValid());
}

}  // namespace
