#include "helpers/asr650x/OledText.h"

#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>

TEST(Asr650xOledText, Behaves) {
  /* glyph table: space is blank, every printable glyph is non-blank, unknown characters map to '?' */
  const uint8_t *sp = asr650x::font5x7Glyph(' ');
  for (int i = 0; i < 5; i++)
    EXPECT_TRUE(sp[i] == 0);
  for (int c = 0x21; c <= 0x7E; c++) {
    const uint8_t *g = asr650x::font5x7Glyph((char)c);
    bool any = false;
    for (int i = 0; i < 5; i++)
      any = any || g[i] != 0;
    EXPECT_TRUE(any);
  }
  EXPECT_TRUE(std::memcmp(asr650x::font5x7Glyph('\x01'), asr650x::font5x7Glyph('?'), 5) == 0);
  EXPECT_TRUE(std::memcmp(asr650x::font5x7Glyph((char)0xC3), asr650x::font5x7Glyph('?'), 5) ==
              0); /* UTF-8 bytes show as '?' */
  EXPECT_TRUE(std::memcmp(asr650x::font5x7Glyph('\x7f'), asr650x::font5x7Glyph('?'), 5) == 0);
  /* distinct shapes for look-alikes */
  EXPECT_TRUE(std::memcmp(asr650x::font5x7Glyph('0'), asr650x::font5x7Glyph('O'), 5) != 0);
  EXPECT_TRUE(std::memcmp(asr650x::font5x7Glyph('l'), asr650x::font5x7Glyph('I'), 5) != 0);

  /* one page = 128 columns: 5 glyph columns + 1 gap per character */
  uint8_t page[128];
  asr650x::renderLine("A", page);
  for (int i = 0; i < 5; i++)
    EXPECT_TRUE(page[i] == asr650x::font5x7Glyph('A')[i]);
  EXPECT_TRUE(page[5] == 0);
  for (int i = 6; i < 128; i++)
    EXPECT_TRUE(page[i] == 0);
  asr650x::renderLine("AB", page);
  for (int i = 0; i < 5; i++)
    EXPECT_TRUE(page[6 + i] == asr650x::font5x7Glyph('B')[i]);
  /* empty and null text give a blank page */
  asr650x::renderLine("", page);
  for (int i = 0; i < 128; i++)
    EXPECT_TRUE(page[i] == 0);
  asr650x::renderLine(nullptr, page);
  for (int i = 0; i < 128; i++)
    EXPECT_TRUE(page[i] == 0);
  /* more than 21 characters: cut, last two columns stay blank */
  asr650x::renderLine("ABCDEFGHIJKLMNOPQRSTUVWXYZ", page);
  EXPECT_TRUE(page[126] == 0 && page[127] == 0);
  for (int i = 0; i < 5; i++)
    EXPECT_TRUE(page[20 * 6 + i] == asr650x::font5x7Glyph('U')[i]);
  /* a long run of UTF-8 does not overrun the buffer */
  asr650x::renderLine("\xe1\xbb\x87\xe1\xbb\x87\xe1\xbb\x87\xe1\xbb\x87\xe1\xbb\x87\xe1\xbb\x87\xe1\xbb\x87"
                      "\xe1\xbb\x87\xe1\xbb\x87",
                      page);
  EXPECT_TRUE(page[127] == 0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
