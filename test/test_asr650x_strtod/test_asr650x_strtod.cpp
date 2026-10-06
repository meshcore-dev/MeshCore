#include <gtest/gtest.h>
#include <stdlib.h>
#include "helpers/asr650x/SmallStrtod.h"

// The CLI parses numbers such as radio frequencies ("869.618"), bandwidths ("62.5"), dB values ("-3.5") and factors.
TEST(Asr650xStrtod, MatchesLibcOnCliStyleNumbers) {
  const char* in[] = {"0", "1", "-1", "869.618", "62.5", "-3.5", "+7", "0.000001", "123456789", "1e3", "2.5E-2",
                      "  42", "920.25,62.5", ".5", "5.", "-0.0"};
  for (const char* s : in) {
    char *e1, *e2;
    double want = strtod(s, &e1);
    double got = asr650x_strtod(s, &e2);
    EXPECT_NEAR(got, want, 1e-9 * (want < 0 ? -want : want) + 1e-12) << s;
    EXPECT_EQ(e2 - s, e1 - s) << s;
  }
}

TEST(Asr650xStrtod, NoDigitsConsumesNothing) {
  const char* in[] = {"", "abc", "-", ".", "e5", "+."};
  for (const char* s : in) {
    char* e;
    EXPECT_EQ(asr650x_strtod(s, &e), 0.0) << s;
    EXPECT_EQ(e, s) << s;
  }
}

TEST(Asr650xStrtod, ExponentWithoutDigitsIsNotConsumed) {
  char* e;
  const char* s = "12e";
  EXPECT_EQ(asr650x_strtod(s, &e), 12.0);
  EXPECT_EQ(e - s, 2);
}

TEST(Asr650xStrtod, NullEndPointerIsAllowed) {
  EXPECT_EQ(asr650x_strtod("3.25", nullptr), 3.25);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
