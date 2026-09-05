// Host tests for the MQTT custom-topic placeholder expansion
// (src/helpers/MQTTTopicTemplate.h), the pure core of
// MQTTBridge::substituteTopicTemplate.
#include <gtest/gtest.h>
#include <cstring>
#include "helpers/MQTTTopicTemplate.h"

static const char* IATA = "DEN";
static const char* DEV = "abcdef0123456789";
static const char* TOK = "tok123";

TEST(TopicTemplate, SubstitutesAllPlaceholders) {
  char buf[128];
  ASSERT_TRUE(mqttSubstituteTopic("meshcore/{iata}/{device}/{type}", IATA, DEV, TOK, "status",
                                  buf, sizeof(buf)));
  EXPECT_STREQ("meshcore/DEN/abcdef0123456789/status", buf);
}

TEST(TopicTemplate, TokenPlaceholder) {
  char buf[128];
  ASSERT_TRUE(mqttSubstituteTopic("meshrank/uplink/{token}/{device}/packets",
                                  IATA, DEV, TOK, "packets", buf, sizeof(buf)));
  EXPECT_STREQ("meshrank/uplink/tok123/abcdef0123456789/packets", buf);
}

TEST(TopicTemplate, RepeatedPlaceholder) {
  char buf[64];
  ASSERT_TRUE(mqttSubstituteTopic("{iata}-{iata}", IATA, DEV, TOK, "raw", buf, sizeof(buf)));
  EXPECT_STREQ("DEN-DEN", buf);
}

TEST(TopicTemplate, LiteralWithNoPlaceholders) {
  char buf[64];
  ASSERT_TRUE(mqttSubstituteTopic("plain/topic/path", IATA, DEV, TOK, "status", buf, sizeof(buf)));
  EXPECT_STREQ("plain/topic/path", buf);
}

TEST(TopicTemplate, UnknownBracesCopiedVerbatim) {
  char buf[64];
  ASSERT_TRUE(mqttSubstituteTopic("a/{bogus}/{iata}", IATA, DEV, TOK, "status", buf, sizeof(buf)));
  EXPECT_STREQ("a/{bogus}/DEN", buf);
}

TEST(TopicTemplate, TypeStringVaries) {
  char buf[64];
  mqttSubstituteTopic("{type}", IATA, DEV, TOK, "status", buf, sizeof(buf));
  EXPECT_STREQ("status", buf);
  mqttSubstituteTopic("{type}", IATA, DEV, TOK, "packets", buf, sizeof(buf));
  EXPECT_STREQ("packets", buf);
  mqttSubstituteTopic("{type}", IATA, DEV, TOK, "raw", buf, sizeof(buf));
  EXPECT_STREQ("raw", buf);
}

TEST(TopicTemplate, NullValuesSubstituteEmpty) {
  char buf[64];
  ASSERT_TRUE(mqttSubstituteTopic("x/{token}/y", IATA, DEV, nullptr, "status", buf, sizeof(buf)));
  EXPECT_STREQ("x//y", buf);
}

TEST(TopicTemplate, OverflowReturnsFalseNoWrite) {
  // Substituting {device} (16 chars) into a template won't fit an 8-byte buffer.
  char buf[8];
  EXPECT_FALSE(mqttSubstituteTopic("{device}", IATA, DEV, TOK, "status", buf, sizeof(buf)));
}

TEST(TopicTemplate, LiteralOverflowReturnsFalseAndNulTerminates) {
  char buf[5];
  // Literal longer than the buffer: fills up to buf_size-1 and NUL-terminates.
  EXPECT_FALSE(mqttSubstituteTopic("abcdefghij", IATA, DEV, TOK, "status", buf, sizeof(buf)));
  EXPECT_EQ('\0', buf[4]);
  EXPECT_EQ((size_t)4, strlen(buf));
}

TEST(TopicTemplate, LiteralSuffixOverflowAfterSubstitutionReturnsFalse) {
  char buf[8];
  EXPECT_FALSE(mqttSubstituteTopic("{iata}/tail", IATA, DEV, TOK, "status", buf, sizeof(buf)));
  EXPECT_STREQ("DEN/tai", buf);
}

TEST(TopicTemplate, ExactFitLiteralSucceeds) {
  char buf[5];
  EXPECT_TRUE(mqttSubstituteTopic("abcd", IATA, DEV, TOK, "status", buf, sizeof(buf)));
  EXPECT_STREQ("abcd", buf);
}

TEST(TopicTemplate, AlwaysNulTerminatedAndBounded) {
  // Fuzz-ish: many buffer sizes never overrun and always NUL-terminate.
  const char* tmpl = "meshcore/{iata}/{device}/{token}/{type}/tail";
  for (size_t sz = 1; sz <= 80; sz++) {
    char buf[96];
    memset(buf, 0x7f, sizeof(buf));
    mqttSubstituteTopic(tmpl, IATA, DEV, TOK, "packets", buf, sz);
    EXPECT_LT(strlen(buf), sz) << "size " << sz;   // fits with room for NUL
    EXPECT_EQ('\0', buf[strlen(buf)]);
  }
}

TEST(TopicTemplate, ZeroBufferOrNullFails) {
  char buf[8];
  EXPECT_FALSE(mqttSubstituteTopic("x", IATA, DEV, TOK, "status", buf, 0));
  EXPECT_FALSE(mqttSubstituteTopic("x", IATA, DEV, TOK, "status", nullptr, 8));
}

TEST(TopicTemplate, EmptyTemplateReturnsFalse) {
  char buf[8];
  EXPECT_FALSE(mqttSubstituteTopic("", IATA, DEV, TOK, "status", buf, sizeof(buf)));
  EXPECT_STREQ("", buf);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
