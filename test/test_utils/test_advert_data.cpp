#include <cstddef>
#include <cstdint>

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
    for (size_t i = 0; i < length; ++i) {
        dest[*offset + i] = bytes[i];
    }
    *offset += length;
}

template <size_t N>
void WriteStringLiteral(uint8_t* dest, size_t* offset, const char (&value)[N]) {
    static_assert(N > 0, "string literal must include a null terminator");
    WriteBytes(dest, offset, reinterpret_cast<const uint8_t*>(value), N - 1);
}

TEST(AdvertData, RoundTripsNameOnly) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: chat advert with a trailing name field.
    WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_NAME_MASK);
    // name field: raw bytes for "alice", consuming the rest of app_data.
    WriteStringLiteral(app_data, &offset, "alice");

    AdvertDataParser parser(app_data, offset);

    ASSERT_TRUE(parser.isValid());
    EXPECT_EQ(ADV_TYPE_CHAT, parser.getType());
    EXPECT_TRUE(parser.hasName());
    EXPECT_STREQ("alice", parser.getName());
    EXPECT_FALSE(parser.hasLatLon());
}

TEST(AdvertData, RoundTripsNameAndCoordinates) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: repeater advert with lat/lon followed by a name.
    WriteU8(app_data, &offset, ADV_TYPE_REPEATER | ADV_LATLON_MASK | ADV_NAME_MASK);
    // latitude field: signed little-endian microdegrees for 37.7749.
    WriteI32Le(app_data, &offset, 37774900);
    // longitude field: signed little-endian microdegrees for -122.4194.
    WriteI32Le(app_data, &offset, -122419400);
    // name field: raw bytes for "node" after the coordinate fields.
    WriteStringLiteral(app_data, &offset, "node");

    AdvertDataParser parser(app_data, offset);

    ASSERT_TRUE(parser.isValid());
    EXPECT_EQ(ADV_TYPE_REPEATER, parser.getType());
    EXPECT_TRUE(parser.hasName());
    EXPECT_STREQ("node", parser.getName());
    EXPECT_TRUE(parser.hasLatLon());
    EXPECT_EQ(37774900, parser.getIntLat());
    EXPECT_EQ(-122419400, parser.getIntLon());
    EXPECT_DOUBLE_EQ(37.7749, parser.getLat());
    EXPECT_DOUBLE_EQ(-122.4194, parser.getLon());
}

TEST(AdvertData, RoundTripsCoordinateExtremes) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: sensor advert with both location fields and a name.
    WriteU8(app_data, &offset, ADV_TYPE_SENSOR | ADV_LATLON_MASK | ADV_NAME_MASK);
    // latitude field: minimum supported latitude, -90.000000 degrees.
    WriteI32Le(app_data, &offset, -90000000);
    // longitude field: maximum supported longitude, 180.000000 degrees.
    WriteI32Le(app_data, &offset, 180000000);
    // name field: raw bytes for "edge".
    WriteStringLiteral(app_data, &offset, "edge");

    AdvertDataParser parser(app_data, offset);

    ASSERT_TRUE(parser.isValid());
    EXPECT_TRUE(parser.hasLatLon());
    EXPECT_EQ(-90000000, parser.getIntLat());
    EXPECT_EQ(180000000, parser.getIntLon());
}

TEST(AdvertData, RejectsLongitudeOutsideValidRange) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: chat advert with location and name fields present.
    WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
    // latitude field: valid latitude so the failure comes from longitude.
    WriteI32Le(app_data, &offset, 37774900);
    // longitude field: one microdegree above +180.0, which is invalid.
    WriteI32Le(app_data, &offset, 180000001);
    // name field: parser should reject before the trailing name matters.
    WriteStringLiteral(app_data, &offset, "node");

    AdvertDataParser parser(app_data, offset);

    EXPECT_FALSE(parser.isValid());
}

TEST(AdvertData, RejectsLongitudeBelowValidRange) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: chat advert with location and name fields present.
    WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
    // latitude field: valid latitude so the failure comes from longitude.
    WriteI32Le(app_data, &offset, 37774900);
    // longitude field: one microdegree below -180.0, which is invalid.
    WriteI32Le(app_data, &offset, -180000001);
    // name field: included to keep the payload shape consistent.
    WriteStringLiteral(app_data, &offset, "node");

    AdvertDataParser parser(app_data, offset);

    EXPECT_FALSE(parser.isValid());
}

TEST(AdvertData, RejectsLatitudeOutsideValidRange) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: chat advert with location and name fields present.
    WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
    // latitude field: one microdegree above +90.0, which is invalid.
    WriteI32Le(app_data, &offset, 90000001);
    // longitude field: valid longitude so the failure comes from latitude.
    WriteI32Le(app_data, &offset, -122419400);
    // name field: included to keep the payload shape consistent.
    WriteStringLiteral(app_data, &offset, "node");

    AdvertDataParser parser(app_data, offset);

    EXPECT_FALSE(parser.isValid());
}

TEST(AdvertData, RejectsLatitudeBelowValidRange) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    size_t offset = 0;

    // flags/type byte: chat advert with location and name fields present.
    WriteU8(app_data, &offset, ADV_TYPE_CHAT | ADV_LATLON_MASK | ADV_NAME_MASK);
    // latitude field: one microdegree below -90.0, which is invalid.
    WriteI32Le(app_data, &offset, -90000001);
    // longitude field: valid longitude so the failure comes from latitude.
    WriteI32Le(app_data, &offset, -122419400);
    // name field: included to keep the payload shape consistent.
    WriteStringLiteral(app_data, &offset, "node");

    AdvertDataParser parser(app_data, offset);

    EXPECT_FALSE(parser.isValid());
}

}  // namespace
