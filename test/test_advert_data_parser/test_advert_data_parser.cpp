#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "helpers/AdvertDataHelpers.h"

namespace {

void expectTruncatedInputsRejected(uint8_t flags, size_t encoded_len) {
  std::vector<uint8_t> encoded(encoded_len, 0);
  encoded[0] = flags;

  for (size_t len = 0; len < encoded_len; ++len) {
    const uint8_t* data = len == 0 ? nullptr : encoded.data();
    AdvertDataParser parser(data, static_cast<uint8_t>(len));
    EXPECT_FALSE(parser.isValid()) << "accepted truncated length " << len;
  }

  AdvertDataParser complete(encoded.data(), static_cast<uint8_t>(encoded.size()));
  EXPECT_TRUE(complete.isValid());
}

}  // namespace

TEST(AdvertDataParser, RejectsNullAndEmptyInput) {
  AdvertDataParser null_input(nullptr, 0);
  EXPECT_FALSE(null_input.isValid());

  const uint8_t unused = 0;
  AdvertDataParser empty_input(&unused, 0);
  EXPECT_FALSE(empty_input.isValid());
}

TEST(AdvertDataParser, RejectsEveryTruncatedOptionalFieldCombination) {
  expectTruncatedInputsRejected(ADV_LATLON_MASK, 1 + 8);
  expectTruncatedInputsRejected(ADV_FEAT1_MASK, 1 + 2);
  expectTruncatedInputsRejected(ADV_FEAT2_MASK, 1 + 2);
  expectTruncatedInputsRejected(ADV_LATLON_MASK | ADV_FEAT1_MASK | ADV_FEAT2_MASK,
                                1 + 8 + 2 + 2);
}

TEST(AdvertDataParser, DecodesCompleteFieldsAfterBoundsChecks) {
  const int32_t expected_lat = 51501234;
  const int32_t expected_lon = -123456;
  const uint16_t expected_feat1 = 0x1234;
  const uint16_t expected_feat2 = 0xabcd;
  const char expected_name[] = "repeater";

  std::vector<uint8_t> encoded(1 + 8 + 2 + 2 + sizeof(expected_name) - 1);
  size_t offset = 0;
  encoded[offset++] = ADV_LATLON_MASK | ADV_FEAT1_MASK | ADV_FEAT2_MASK | ADV_NAME_MASK;
  memcpy(&encoded[offset], &expected_lat, sizeof(expected_lat));
  offset += sizeof(expected_lat);
  memcpy(&encoded[offset], &expected_lon, sizeof(expected_lon));
  offset += sizeof(expected_lon);
  memcpy(&encoded[offset], &expected_feat1, sizeof(expected_feat1));
  offset += sizeof(expected_feat1);
  memcpy(&encoded[offset], &expected_feat2, sizeof(expected_feat2));
  offset += sizeof(expected_feat2);
  memcpy(&encoded[offset], expected_name, sizeof(expected_name) - 1);

  AdvertDataParser parser(encoded.data(), static_cast<uint8_t>(encoded.size()));
  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(expected_lat, parser.getIntLat());
  EXPECT_EQ(expected_lon, parser.getIntLon());
  EXPECT_EQ(expected_feat1, parser.getFeat1());
  EXPECT_EQ(expected_feat2, parser.getFeat2());
  EXPECT_STREQ(expected_name, parser.getName());
}

TEST(AdvertDataParser, RejectsInputBeyondProtocolLimit) {
  std::vector<uint8_t> encoded(MAX_ADVERT_DATA_SIZE + 1, ADV_NAME_MASK);
  AdvertDataParser parser(encoded.data(), static_cast<uint8_t>(encoded.size()));
  EXPECT_FALSE(parser.isValid());
}

TEST(AdvertDataParser, AcceptsMaximumLengthNameAndTerminatesIt) {
  std::vector<uint8_t> encoded(MAX_ADVERT_DATA_SIZE, 'a');
  encoded[0] = ADV_NAME_MASK;

  AdvertDataParser parser(encoded.data(), static_cast<uint8_t>(encoded.size()));
  ASSERT_TRUE(parser.isValid());
  EXPECT_EQ(MAX_ADVERT_DATA_SIZE - 1, strlen(parser.getName()));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
