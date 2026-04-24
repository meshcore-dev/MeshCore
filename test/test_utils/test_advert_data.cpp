#include <gtest/gtest.h>

#include "helpers/AdvertDataHelpers.h"

namespace {

TEST(AdvertData, RoundTripsNameOnly) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    AdvertDataBuilder builder(ADV_TYPE_CHAT, "alice");

    uint8_t len = builder.encodeTo(app_data);
    AdvertDataParser parser(app_data, len);

    ASSERT_TRUE(parser.isValid());
    EXPECT_EQ(ADV_TYPE_CHAT, parser.getType());
    EXPECT_TRUE(parser.hasName());
    EXPECT_STREQ("alice", parser.getName());
    EXPECT_FALSE(parser.hasLatLon());
}

TEST(AdvertData, RoundTripsNameAndCoordinates) {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    AdvertDataBuilder builder(ADV_TYPE_REPEATER, "node", 37.7749, -122.4194);

    uint8_t len = builder.encodeTo(app_data);
    AdvertDataParser parser(app_data, len);

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
    AdvertDataBuilder builder(ADV_TYPE_SENSOR, "edge", -90.0, 180.0);

    uint8_t len = builder.encodeTo(app_data);
    AdvertDataParser parser(app_data, len);

    ASSERT_TRUE(parser.isValid());
    EXPECT_TRUE(parser.hasLatLon());
    EXPECT_EQ(-90000000, parser.getIntLat());
    EXPECT_EQ(180000000, parser.getIntLon());
}

}  // namespace
