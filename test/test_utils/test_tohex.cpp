#include <gtest/gtest.h>
#include "Utils.h"

using namespace mesh;

#define HEX_BUFFER_SIZE(input) (sizeof(input) * 2 + 1)

TEST(UtilsToHex, ConvertSingleByte) {
    uint8_t input[] = {0xAB};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("AB", output);
}

TEST(UtilsToHex, ConvertMultipleBytes) {
    uint8_t input[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("0123456789ABCDEF", output);
}

TEST(UtilsToHex, ConvertZeroByte) {
    uint8_t input[] = {0x00};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("00", output);
}

TEST(UtilsToHex, ConvertMaxByte) {
    uint8_t input[] = {0xFF};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("FF", output);
}

TEST(UtilsToHex, NullTerminatesOnEmptyInput) {
    uint8_t input[] = {0xAB};
    char output[] = "X";  // Pre-fill with X.

    Utils::toHex(output, input, 0);

    // Should just null-terminate at position 0
    EXPECT_EQ('\0', output[0]);
}

TEST(UtilsParseTextParts, PreservesEmptyPartsByDefault) {
    char input[] = "region default  ";
    const char* parts[4];

    int count = Utils::parseTextParts(input, parts, 4, ' ');

    ASSERT_EQ(3, count);
    EXPECT_STREQ("region", parts[0]);
    EXPECT_STREQ("default", parts[1]);
    EXPECT_STREQ("", parts[2]);
}

TEST(UtilsParseTextParts, SkippingEmptyTreatsTrailingSpacesAsNoArgument) {
    char input[] = "region default  ";
    const char* parts[4];

    int count = Utils::parseTextPartsSkippingEmpty(input, parts, 4, ' ');

    ASSERT_EQ(2, count);
    EXPECT_STREQ("region", parts[0]);
    EXPECT_STREQ("default", parts[1]);
}

TEST(UtilsParseTextParts, SkippingEmptyKeepsNamedArgumentBetweenRepeatedSpaces) {
    char input[] = "region  default  au-nsw  ";
    const char* parts[4];

    int count = Utils::parseTextPartsSkippingEmpty(input, parts, 4, ' ');

    ASSERT_EQ(3, count);
    EXPECT_STREQ("region", parts[0]);
    EXPECT_STREQ("default", parts[1]);
    EXPECT_STREQ("au-nsw", parts[2]);
}

TEST(UtilsParseTextParts, KeepsExplicitNullRegionArgument) {
    char input[] = "region  default  <null>  ";
    const char* parts[4];

    int count = Utils::parseTextPartsSkippingEmpty(input, parts, 4, ' ');

    ASSERT_EQ(3, count);
    EXPECT_STREQ("region", parts[0]);
    EXPECT_STREQ("default", parts[1]);
    EXPECT_STREQ("<null>", parts[2]);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
