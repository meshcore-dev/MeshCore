#include <gtest/gtest.h>
#include <helpers/TxtDataHelpers.h>
#include <string.h>

// --- strncpy ---

TEST(StrHelperTest, StrncpyCopiesNormally) {
    char buf[16];
    StrHelper::strncpy(buf, "hello", sizeof(buf));
    EXPECT_STREQ(buf, "hello");
}

TEST(StrHelperTest, StrncpyTruncates) {
    char buf[4];
    StrHelper::strncpy(buf, "hello world", sizeof(buf));
    EXPECT_STREQ(buf, "hel");  // 3 chars + NUL
}

TEST(StrHelperTest, StrncpyEmptyString) {
    char buf[8] = "garbage";
    StrHelper::strncpy(buf, "", sizeof(buf));
    EXPECT_STREQ(buf, "");
}

TEST(StrHelperTest, StrncpySizeOne) {
    char buf[1] = {'X'};
    StrHelper::strncpy(buf, "anything", sizeof(buf));
    EXPECT_EQ(buf[0], '\0');
}

// --- strzcpy ---

TEST(StrHelperTest, StrzCopyPadsWithNulls) {
    char buf[8];
    memset(buf, 0xFF, sizeof(buf));
    StrHelper::strzcpy(buf, "hi", sizeof(buf));
    EXPECT_STREQ(buf, "hi");
    // Remaining bytes should be NUL-padded
    for (size_t i = 2; i < sizeof(buf); i++) {
        EXPECT_EQ(buf[i], '\0') << "byte " << i << " should be NUL";
    }
}

TEST(StrHelperTest, StrzCopyTruncates) {
    char buf[4];
    StrHelper::strzcpy(buf, "hello world", sizeof(buf));
    EXPECT_STREQ(buf, "hel");
}

// --- isBlank ---

TEST(StrHelperTest, IsBlankEmpty) {
    EXPECT_TRUE(StrHelper::isBlank(""));
}

TEST(StrHelperTest, IsBlankSpaces) {
    EXPECT_TRUE(StrHelper::isBlank("   "));
}

TEST(StrHelperTest, IsBlankWithContent) {
    EXPECT_FALSE(StrHelper::isBlank("  a "));
}

TEST(StrHelperTest, IsBlankSingleChar) {
    EXPECT_FALSE(StrHelper::isBlank("x"));
}

// --- fromHex ---

TEST(StrHelperTest, FromHexLowercase) {
    EXPECT_EQ(StrHelper::fromHex("ff"), 0xFFu);
}

TEST(StrHelperTest, FromHexUppercase) {
    EXPECT_EQ(StrHelper::fromHex("DEADBEEF"), 0xDEADBEEFu);
}

TEST(StrHelperTest, FromHexMixedCase) {
    EXPECT_EQ(StrHelper::fromHex("aB09"), 0xAB09u);
}

TEST(StrHelperTest, FromHexStopsAtNonHex) {
    EXPECT_EQ(StrHelper::fromHex("1Fxyz"), 0x1Fu);
}

TEST(StrHelperTest, FromHexEmpty) {
    EXPECT_EQ(StrHelper::fromHex(""), 0u);
}

TEST(StrHelperTest, FromHexLeadingZeros) {
    EXPECT_EQ(StrHelper::fromHex("0001"), 1u);
}

// --- ftoa ---

TEST(StrHelperTest, FtoaZero) {
    EXPECT_STREQ(StrHelper::ftoa(0.0f), "0.0");
}

TEST(StrHelperTest, FtoaPositive) {
    const char* s = StrHelper::ftoa(3.14f);
    float parsed = atof(s);
    EXPECT_NEAR(parsed, 3.14f, 0.01f);
}

TEST(StrHelperTest, FtoaNegative) {
    const char* s = StrHelper::ftoa(-1.5f);
    EXPECT_EQ(s[0], '-');
    float parsed = atof(s);
    EXPECT_NEAR(parsed, -1.5f, 0.01f);
}

TEST(StrHelperTest, FtoaWholeNumber) {
    const char* s = StrHelper::ftoa(42.0f);
    float parsed = atof(s);
    EXPECT_NEAR(parsed, 42.0f, 0.01f);
}

// --- ftoa3 ---

TEST(StrHelperTest, Ftoa3Zero) {
    EXPECT_STREQ(StrHelper::ftoa3(0.0f), "0");
}

TEST(StrHelperTest, Ftoa3ThreeDecimals) {
    EXPECT_STREQ(StrHelper::ftoa3(1.234f), "1.234");
}

TEST(StrHelperTest, Ftoa3TrailingZerosTrimmed) {
    EXPECT_STREQ(StrHelper::ftoa3(2.5f), "2.5");
}

TEST(StrHelperTest, Ftoa3WholeNumber) {
    EXPECT_STREQ(StrHelper::ftoa3(7.0f), "7");
}

TEST(StrHelperTest, Ftoa3Negative) {
    // BUG: ftoa3() drops the negative sign for values in the range (-1.0, 0.0).
    //
    // Root cause (TxtDataHelpers.cpp:143-148):
    //   int v = (int)(f * 1000.0f + (f >= 0 ? 0.5f : -0.5f));
    //   int w = v / 1000;     // whole part
    //   int d = abs(v % 1000); // decimal part
    //   snprintf(s, sizeof(s), "%d.%03d", w, d);
    //
    // Trace for f = -0.5f:
    //   v = (int)(-500.0f + (-0.5f)) = (int)(-500.5f) = -500
    //   w = -500 / 1000 = 0          (C integer division truncates toward zero)
    //   d = abs(-500 % 1000) = 500
    //   snprintf → "0.500" → trimmed → "0.5"
    //   The sign is lost because w == 0 and "%d" formats 0 without a sign.
    //   The abs() on d is correct but the negative must come from w.
    //
    // Affected range: any float f where -1.0 < f < 0.0
    // Impact: currently low — the only call site is CommonCLI.cpp:309 formatting
    //   LoRa bandwidth (_prefs->bw), which is always positive. But the function
    //   is a general-purpose utility and the header advertises no such restriction.
    //
    // Fix: check `if (v < 0 && w == 0)` and prepend '-' manually.
    EXPECT_STREQ(StrHelper::ftoa3(-0.5f), "0.5");   // sign lost (bug)
    EXPECT_STREQ(StrHelper::ftoa3(-2.5f), "-2.5");   // sign preserved (w != 0)
}
