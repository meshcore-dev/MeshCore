#include <gtest/gtest.h>

#include "mock_streams.h"

//using namespace testing;

TEST(MockStreamTests, ExternalBuffer)
{
  uint8_t buf[21];
  MockStream s(buf);
  s.write((uint8_t*)"0123456789", 10);
  ASSERT_EQ(s.pos, 10);
  ASSERT_STREQ((const char *)s.buffer, (const char*)"0123456789");
  s.write((uint8_t*)"ABCDEFGHIJ", 10);
  ASSERT_EQ(s.pos, 20);
  EXPECT_THAT(buf, MemcmpAs("0123456789ABCDEFGHIJ", 20));

  MockStream s2(buf, 4);
  ASSERT_EQ(s2.write((uint8_t *)"12345", 5), 0);
  ASSERT_EQ(s2.pos, 0);
  ASSERT_EQ(s2.cap, 4);
  ASSERT_EQ(s2.write((uint8_t *)"1234", 4), 4);
  ASSERT_EQ(s2.pos, 4);
  EXPECT_THAT(buf, MemcmpAs("1234", 4));
}

TEST(MockStreamTests, InternalBuffer)
{
  MockStream s1;
  uint8_t z[65];
  memset(z, 0, sizeof(z));
  s1.write(z, sizeof(z));
  ASSERT_EQ(s1.pos, sizeof(z));
  ASSERT_GE(s1.cap, sizeof(z));

  MockStream s2;
  for (int i = 0; i < 1024; i++) {
    s2.write('A');
  }
  ASSERT_EQ(s2.pos, 1024);
  ASSERT_GE(s2.cap, 1024);
  ASSERT_EQ(s2.buffer[1023], 'A');
}

