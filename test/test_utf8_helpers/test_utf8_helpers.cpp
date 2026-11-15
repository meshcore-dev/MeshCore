#include <gtest/gtest.h>
#include <string>

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

TEST(UTF8Helpers, PrefixedNameKeepsShortAndExactLimitNames) {
  char output[48];
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", "Radio", 32);
  EXPECT_STREQ("MeshCore-Radio", output);
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", "abcdefghijklmnopqrstuvw", 32);
  EXPECT_STREQ("MeshCore-abcdefghijklmnopqrstuvw", output);
  EXPECT_EQ(32u, std::string(output).size());
}

TEST(UTF8Helpers, PrefixedNameDoesNotSplitEmojiOrChangeNodeName) {
  char output[48];
  const std::string name = "abcdefghijklmnopqrstu 🌴";
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", name.c_str(), 32);
  EXPECT_STREQ("MeshCore-abcdefghijklmnopqrstu ", output);
  EXPECT_EQ("abcdefghijklmnopqrstu 🌴", name);
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", "abcdefghijklmnopqrs🌴", 32);
  EXPECT_STREQ("MeshCore-abcdefghijklmnopqrs🌴", output);
  EXPECT_EQ(32u, std::string(output).size());
}

TEST(UTF8Helpers, PrefixedNameHandlesTwoAndThreeByteBoundaries) {
  char output[48];
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", "abcdefghijklmnopqrstuvé", 32);
  EXPECT_STREQ("MeshCore-abcdefghijklmnopqrstuv", output);
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", "abcdefghijklmnopqrstu界", 32);
  EXPECT_STREQ("MeshCore-abcdefghijklmnopqrstu", output);
}

TEST(UTF8Helpers, PrefixedNameHonorsSmallerDestinationAndLongPrefix) {
  char output[6] = {'?', '?', '?', '?', '?', '!'};
  mesh::formatUtf8Prefixed(output, 5, "🌴", "Radio", 32);
  EXPECT_STREQ("🌴", output);
  EXPECT_EQ('!', output[5]);
  mesh::formatUtf8Prefixed(output, 5, "abcdef", "Radio", 32);
  EXPECT_STREQ("abcd", output);
  EXPECT_EQ('!', output[5]);
}

TEST(UTF8Helpers, PrefixedNameHonorsZeroLimitAndCapacity) {
  char output[2] = {'!', '!'};
  mesh::formatUtf8Prefixed(output, 0, "MeshCore-", "Radio", 32);
  EXPECT_EQ('!', output[0]);
  mesh::formatUtf8Prefixed(output, 1, "MeshCore-", "Radio", 32);
  EXPECT_EQ('\0', output[0]); EXPECT_EQ('!', output[1]);
  mesh::formatUtf8Prefixed(output, sizeof(output), "MeshCore-", "Radio", 0);
  EXPECT_EQ('\0', output[0]); EXPECT_EQ('!', output[1]);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
