#include <gtest/gtest.h>

#include <helpers/UTF8Helpers.h>

TEST(UTF8Helpers, KeepsCompleteNameWithinLimit) {
  const char* name = "Example RPT 🔋🇵🇱";

  EXPECT_EQ(24u, mesh::validUtf8PrefixLength(name, 24));
}

TEST(UTF8Helpers, StopsBeforeCodePointCrossingLimit) {
  const char* name = "Example RPT 🔋🇵🇱";

  EXPECT_EQ(20u, mesh::validUtf8PrefixLength(name, 23));
}

TEST(UTF8Helpers, RejectsMalformedAndTruncatedSequences) {
  const char overlong[] = {'A', static_cast<char>(0xC0), static_cast<char>(0xAF), 0};
  const char surrogate[] = {'A', static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80), 0};
  const char out_of_range[] = {'A', static_cast<char>(0xF4), static_cast<char>(0x90), static_cast<char>(0x80), static_cast<char>(0x80), 0};
  const char truncated[] = {'A', static_cast<char>(0xF0), static_cast<char>(0x9F), 0};

  EXPECT_EQ(1u, mesh::validUtf8PrefixLength(overlong, sizeof(overlong)));
  EXPECT_EQ(1u, mesh::validUtf8PrefixLength(surrogate, sizeof(surrogate)));
  EXPECT_EQ(1u, mesh::validUtf8PrefixLength(out_of_range, sizeof(out_of_range)));
  EXPECT_EQ(1u, mesh::validUtf8PrefixLength(truncated, sizeof(truncated)));
}

TEST(UTF8Helpers, RejectsUnexpectedContinuationByte) {
  const char invalid[] = {'A', static_cast<char>(0x80), 'B', 0};

  EXPECT_EQ(1u, mesh::validUtf8PrefixLength(invalid, sizeof(invalid)));
}

TEST(UTF8Helpers, TruncateKeepsTextThatFits) {
  const char* s = "ab\xC3\xA9" "cd";  // "abécd", 6 bytes
  EXPECT_EQ(6u, mesh::utf8TruncatedLength(s, 6, 10));
}

TEST(UTF8Helpers, TruncateDoesNotSplitCodePoint) {
  const char* s = "ab\xC3\xA9" "cd";
  EXPECT_EQ(2u, mesh::utf8TruncatedLength(s, 6, 3));  // cut inside é -> drop é
  EXPECT_EQ(4u, mesh::utf8TruncatedLength(s, 6, 4));  // é fits exactly
}

TEST(UTF8Helpers, TruncateDropsWholeEmoji) {
  const char* s = "a\xF0\x9F\x94\x8B";  // "a🔋"
  for (size_t m = 1; m < 5; m++) EXPECT_EQ(1u, mesh::utf8TruncatedLength(s, 5, m));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
