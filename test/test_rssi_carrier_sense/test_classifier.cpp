#include <gtest/gtest.h>

#include "helpers/radiolib/RssiCarrierSense.h"

using namespace mesh;

TEST(RssiCarrierSense, DisabledThresholdIsInactive) {
  int16_t samples[] = {-80, -75, -70};
  RssiCarrierSenseConfig config = {-110, 0, true, 2};

  EXPECT_FALSE(RssiCarrierSense::isActive(config, samples, 3));
}

TEST(RssiCarrierSense, UnconvergedNoiseFloorIsInactive) {
  int16_t samples[] = {-80, -75, -70};
  RssiCarrierSenseConfig config = {-110, 10, false, 2};

  EXPECT_FALSE(RssiCarrierSense::isActive(config, samples, 3));
}

TEST(RssiCarrierSense, BelowThresholdSamplesAreInactive) {
  int16_t samples[] = {-101, -100, -99, -101, -100};
  RssiCarrierSenseConfig config = {-110, 10, true, 3};

  EXPECT_FALSE(RssiCarrierSense::isActive(config, samples, 5));
}

TEST(RssiCarrierSense, IsolatedSpikeIsInactive) {
  int16_t samples[] = {-101, -70, -101, -100, -101};
  RssiCarrierSenseConfig config = {-110, 10, true, 3};

  EXPECT_FALSE(RssiCarrierSense::isActive(config, samples, 5));
}

TEST(RssiCarrierSense, MajorityAboveThresholdIsActive) {
  int16_t samples[] = {-99, -98, -101, -97, -102};
  RssiCarrierSenseConfig config = {-110, 10, true, 3};

  EXPECT_TRUE(RssiCarrierSense::isActive(config, samples, 5));
}

TEST(RssiCarrierSense, ExactThresholdBoundaryIsInactive) {
  int16_t samples[] = {-100, -100, -100, -101, -102};
  RssiCarrierSenseConfig config = {-110, 10, true, 3};

  EXPECT_FALSE(RssiCarrierSense::isActive(config, samples, 5));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
