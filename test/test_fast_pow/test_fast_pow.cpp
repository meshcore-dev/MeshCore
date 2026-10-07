#include "helpers/FastPow.h"

#include <gtest/gtest.h>
#include <math.h>

// calcRxDelay() uses 10^(0.85 - score); score is in [0, 1] in practice, the range below is wider on purpose
TEST(FastPow10, WithinOnePercentOfPow) {
  for (int i = -300; i <= 300; i++) {
    float x = i / 100.0f;
    float want = powf(10.0f, x);
    float got = fastPow10f(x);
    EXPECT_NEAR(got / want, 1.0f, 0.01f) << "x=" << x;
  }
}

TEST(FastPow10, ExactAtIntegers) {
  EXPECT_NEAR(fastPow10f(0.0f), 1.0f, 0.001f);
  EXPECT_NEAR(fastPow10f(1.0f), 10.0f, 0.05f);
  EXPECT_NEAR(fastPow10f(-1.0f), 0.1f, 0.0005f);
}

// MyMesh::calcRxDelay() uses rx_delay_base^(0.85 - score) with a user-set base
TEST(FastPow, AnyBaseWithinOnePercentOfPow) {
  const float bases[] = { 1.05f, 1.5f, 2.0f, 5.0f, 10.0f, 20.0f };
  for (float b : bases) {
    for (int i = -200; i <= 200; i++) {
      float x = i / 100.0f;
      EXPECT_NEAR(fastPowf(b, x) / powf(b, x), 1.0f, 0.01f) << "b=" << b << " x=" << x;
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
