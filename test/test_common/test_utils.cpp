#include <gtest/gtest.h>

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
}

TEST(UtilTests, toHex)
{
  char dst[20];
  uint8_t src[] = "\x01\x7f\x80\xff";
  mesh::Utils::toHex(&dst[0], src, 4);
  EXPECT_STREQ(dst, (const char*)"017F80FF");
}

#if defined(ARDUINO)
#include <Arduino.h>

void setup()
{
  Serial.begin(115200);
  ::testing::InitGoogleTest();
}

void loop()
{
  if (RUN_ALL_TESTS())
    ;
  delay(1000);
}

#else

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  // or ::testing::InitGoogleMock(&argc, argv);

  if (RUN_ALL_TESTS())
    ;
  return 0;
}

#endif
