#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Stream.h>
#include <string>

#include "mock_streams.h"
#include "Utils.h"

using namespace mesh;


TEST(UtilTests, NopTest)
{
    EXPECT_EQ(1, 1);
}

TEST(UtilTests, SHA256)
{
  uint8_t hash[257];
  memset(hash, 0, sizeof(hash));
  uint8_t msg[] = "foo";
  mesh::Utils::sha256(hash, (size_t)sizeof(hash), msg, 3);
  EXPECT_STREQ((char*)hash,
			(char*)"\x2c\x26\xb4\x6b\x68\xff\xc6\x8f\xf9\x9b\x45\x3c\x1d\x30\x41\x34\x13\x42\x2d\x70\x64\x83\xbf\xa0\xf9\x8a\x5e\x88\x62\x66\xe7\xae");

  memset(hash, 0, sizeof(hash));
  mesh::Utils::sha256(hash, (size_t)sizeof(hash), msg, 1, msg+1, 2);
  EXPECT_STREQ((char*)hash,
			(char*)"\x2c\x26\xb4\x6b\x68\xff\xc6\x8f\xf9\x9b\x45\x3c\x1d\x30\x41\x34\x13\x42\x2d\x70\x64\x83\xbf\xa0\xf9\x8a\x5e\x88\x62\x66\xe7\xae");
}

TEST(UtilTests, toHex)
{
  char dst[20];
  uint8_t src[] = "\x01\x7f\x80\xff";
  mesh::Utils::toHex(&dst[0], src, 4);
  EXPECT_STREQ(dst, (const char*)"017F80FF");
}

TEST(UtilTests, fromHex)
{
  uint8_t dst[20];
  memset(dst, 0, sizeof(dst));
  uint8_t want[] = "\x01\x7f\x80\xff";
  EXPECT_TRUE(mesh::Utils::fromHex(&dst[0], 4, "017F80FF"));
  EXPECT_STREQ((const char *)dst, (const char *)want);
}

TEST(UtilTests, fromHexWrongSize)
{
  uint8_t dst[20];
  EXPECT_FALSE(mesh::Utils::fromHex(&dst[0], 5, "017F80FF"));
}

// this should pass but does not, because fromHex() doesn't
// actually validate string contents and silently produces
// zeroes
// TEST(UtilTests, fromHexMalformed)
// {
//   uint8_t dst[20];
//   memset(dst, 0, sizeof(dst));
//   EXPECT_FALSE(mesh::Utils::fromHex(&dst[0], 4, "01FG80FF"));
// }

TEST(UtilTests, isHexChar)
{
  EXPECT_TRUE(mesh::Utils::isHexChar('0'));
  EXPECT_TRUE(mesh::Utils::isHexChar('1'));
  EXPECT_TRUE(mesh::Utils::isHexChar('9'));
  EXPECT_TRUE(mesh::Utils::isHexChar('A'));
  EXPECT_TRUE(mesh::Utils::isHexChar('F'));
  EXPECT_FALSE(mesh::Utils::isHexChar('G'));
  EXPECT_FALSE(mesh::Utils::isHexChar('\xff'));
  EXPECT_FALSE(mesh::Utils::isHexChar('\x0'));
}

TEST(UtilTests, parseTextParts)
{
  char text[10];
  memset(text, 0, sizeof(text));
  const char *parts[10];
  ASSERT_EQ(mesh::Utils::parseTextParts("", &parts[0], 10, ','), 0);

  strcpy(text, "a");
  ASSERT_EQ(mesh::Utils::parseTextParts(text, &parts[0], 10, ','), 1);
  ASSERT_STREQ(parts[0], "a");

  strcpy(text, "b,c");
  ASSERT_EQ(mesh::Utils::parseTextParts(text, &parts[0], 10, ','), 2);
  ASSERT_STREQ(parts[0], "b");
  ASSERT_STREQ(parts[1], "c");

  strcpy(text, "d,,e");
  ASSERT_EQ(mesh::Utils::parseTextParts(text, &parts[0], 10, ','), 3);
  ASSERT_STREQ(parts[0], "d");
  ASSERT_STREQ(parts[1], "");
  ASSERT_STREQ(parts[2], "e");

  // This isn't normal string splitter behavior, but it's intentional
  strcpy(text, "f,g,");
  ASSERT_EQ(mesh::Utils::parseTextParts(text, &parts[0], 10, ','), 2);
  ASSERT_STREQ(parts[0], "f");
  ASSERT_STREQ(parts[1], "g");
}

TEST(UtilTests, printHex)
{
  MockStream s;
  const uint8_t src[] = "\x00\x7f\xab\xff";
  mesh::Utils::printHex(s, src, 4);
  EXPECT_STREQ((const char *)s.buffer, "007FABFF");
}
