#include "gps_test_support.h"

TEST(MicroNMEAProvider, ParsesWellFormedSentences) {
  g_mock_millis = 1000;
  MockSerial serial;
  MicroNMEALocationProvider gps(serial, nullptr, -1, -1);
  feedAndLoop(gps, serial, withChecksum(kGoodGga));
  feedAndLoop(gps, serial, withChecksum(kGoodRmc));

  EXPECT_TRUE(gps.isValid());
  EXPECT_EQ(47268723L, gps.getLatitude());     // 47 deg 16.1234' N
  EXPECT_EQ(-122559463L, gps.getLongitude());  // 122 deg 33.5678' W
  EXPECT_EQ(8L, gps.satellitesCount());
  EXPECT_EQ(125000L, gps.getAltitude());
  EXPECT_EQ(1792238434L, gps.getTimestamp());  // 2026-10-17 12:00:34 UTC
}

TEST(MicroNMEAProvider, LongestSentenceTheBufferHoldsStillParses) {
  std::string body = kGoodRmc;
  while (body.size() < 99 - 4) body.push_back('Z');  // pad the final field
  const std::string sentence = withChecksum(body);
  ASSERT_EQ(99u, sentence.size());

  g_mock_millis = 1000;
  MockSerial serial;
  MicroNMEALocationProvider gps(serial, nullptr, -1, -1);
  feedAndLoop(gps, serial, sentence);
  EXPECT_TRUE(gps.isValid());
  EXPECT_EQ(47268723L, gps.getLatitude());
}
