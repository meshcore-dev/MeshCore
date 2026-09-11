#include <gtest/gtest.h>
#include "helpers/sensors/LPPDataHelpers.h"

TEST(LPPWriterWindTest, WriteWindSpeedEncodesChannelTypeAndBigEndianValue) {
  uint8_t buf[4] = {};
  LPPWriter writer(buf, sizeof(buf));

  EXPECT_TRUE(writer.writeWindSpeed(2, 0x1234));
  EXPECT_EQ(writer.length(), 4);
  EXPECT_EQ(buf[0], 2);              // channel
  EXPECT_EQ(buf[1], LPP_WIND_SPEED); // type
  EXPECT_EQ(buf[2], 0x12);           // MSB
  EXPECT_EQ(buf[3], 0x34);           // LSB
}

TEST(LPPWriterWindTest, WriteWindGustEncodesChannelTypeAndBigEndianValue) {
  uint8_t buf[4] = {};
  LPPWriter writer(buf, sizeof(buf));

  EXPECT_TRUE(writer.writeWindGust(3, 0xABCD));
  EXPECT_EQ(writer.length(), 4);
  EXPECT_EQ(buf[0], 3);
  EXPECT_EQ(buf[1], LPP_WIND_GUST);
  EXPECT_EQ(buf[2], 0xAB);
  EXPECT_EQ(buf[3], 0xCD);
}

TEST(LPPWriterWindTest, WriteRainEncodesChannelTypeAndBigEndianValue) {
  uint8_t buf[4] = {};
  LPPWriter writer(buf, sizeof(buf));

  EXPECT_TRUE(writer.writeRain(1, 0));
  EXPECT_EQ(writer.length(), 4);
  EXPECT_EQ(buf[0], 1);
  EXPECT_EQ(buf[1], LPP_RAIN);
  EXPECT_EQ(buf[2], 0x00);
  EXPECT_EQ(buf[3], 0x00);
}

TEST(LPPWriterWindTest, WriteDirectionEncodesChannelTypeAndBigEndianValue) {
  uint8_t buf[4] = {};
  LPPWriter writer(buf, sizeof(buf));

  EXPECT_TRUE(writer.writeDirection(1, 359));
  EXPECT_EQ(writer.length(), 4);
  EXPECT_EQ(buf[0], 1);
  EXPECT_EQ(buf[1], LPP_DIRECTION);
  EXPECT_EQ(buf[2], 359 >> 8);
  EXPECT_EQ(buf[3], 359 & 0xFF);
}

TEST(LPPWriterWindTest, MultipleWritesAppendSequentiallyWithoutOverwriting) {
  uint8_t buf[16] = {};
  LPPWriter writer(buf, sizeof(buf));

  writer.writeDirection(4, 180);
  writer.writeWindSpeed(4, 50);
  writer.writeWindGust(4, 80);
  writer.writeRain(4, 3);

  ASSERT_EQ(writer.length(), 16);

  LPPReader reader(buf, sizeof(buf));
  uint8_t channel, type;

  ASSERT_TRUE(reader.readHeader(channel, type));
  EXPECT_EQ(channel, 4);
  EXPECT_EQ(type, LPP_DIRECTION);
  reader.skipData(type);

  ASSERT_TRUE(reader.readHeader(channel, type));
  EXPECT_EQ(channel, 4);
  EXPECT_EQ(type, LPP_WIND_SPEED);
  reader.skipData(type);

  ASSERT_TRUE(reader.readHeader(channel, type));
  EXPECT_EQ(channel, 4);
  EXPECT_EQ(type, LPP_WIND_GUST);
  reader.skipData(type);

  ASSERT_TRUE(reader.readHeader(channel, type));
  EXPECT_EQ(channel, 4);
  EXPECT_EQ(type, LPP_RAIN);
  reader.skipData(type);
}

TEST(LPPWriterWindTest, RefusesToWritePastCapacity) {
  uint8_t buf[3] = {};  // too small for a 4-byte field
  LPPWriter writer(buf, sizeof(buf));

  EXPECT_FALSE(writer.writeWindSpeed(1, 100));
  EXPECT_EQ(writer.length(), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
