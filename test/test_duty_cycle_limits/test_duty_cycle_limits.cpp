#include <gtest/gtest.h>
#include "helpers/DutyCycleLimits.h"

TEST(DutyCycleLimits, UnregulatedOutsideSrdBand) {
    EXPECT_FLOAT_EQ(DUTY_CYCLE_UNLIMITED, getMaxDutyCyclePercent(433.0f));
    EXPECT_FLOAT_EQ(DUTY_CYCLE_UNLIMITED, getMaxDutyCyclePercent(862.9f));
    EXPECT_FLOAT_EQ(DUTY_CYCLE_UNLIMITED, getMaxDutyCyclePercent(870.1f));
    EXPECT_FLOAT_EQ(DUTY_CYCLE_UNLIMITED, getMaxDutyCyclePercent(910.525f));
    EXPECT_FLOAT_EQ(DUTY_CYCLE_UNLIMITED, getMaxDutyCyclePercent(915.0f));
}

TEST(DutyCycleLimits, SubBandLimits) {
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(864.0f));
    EXPECT_FLOAT_EQ(1.0f, getMaxDutyCyclePercent(867.0f));
    EXPECT_FLOAT_EQ(1.0f, getMaxDutyCyclePercent(868.3f));
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(869.0f));
    EXPECT_FLOAT_EQ(10.0f, getMaxDutyCyclePercent(869.525f));
    EXPECT_FLOAT_EQ(10.0f, getMaxDutyCyclePercent(869.618f));   // the shipped LORA_FREQ default
    EXPECT_FLOAT_EQ(1.0f, getMaxDutyCyclePercent(869.8f));
}

TEST(DutyCycleLimits, BoundaryTakesTheLowerSubBand) {
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(863.0f));
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(865.0f));
    EXPECT_FLOAT_EQ(1.0f, getMaxDutyCyclePercent(868.0f));
    EXPECT_FLOAT_EQ(10.0f, getMaxDutyCyclePercent(869.65f));
    EXPECT_FLOAT_EQ(1.0f, getMaxDutyCyclePercent(870.0f));
}

TEST(DutyCycleLimits, GapsInsideBandFallBackToTheTightestLimit) {
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(868.65f));
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(869.3f));
    EXPECT_FLOAT_EQ(0.1f, getMaxDutyCyclePercent(869.68f));
}

TEST(DutyCycleLimits, AirtimeFactorConversion) {
    EXPECT_FLOAT_EQ(99.0f, dutyCycleToAirtimeFactor(1.0f));
    EXPECT_FLOAT_EQ(9.0f, dutyCycleToAirtimeFactor(10.0f));
    EXPECT_FLOAT_EQ(1.0f, dutyCycleToAirtimeFactor(50.0f));
    EXPECT_FLOAT_EQ(0.0f, dutyCycleToAirtimeFactor(100.0f));
}

TEST(DutyCycleLimits, AutoDerivesTheFactorFromFreq) {
    EXPECT_FLOAT_EQ(9.0f, getEffectiveAirtimeFactor(1, 1.0f, 869.618f));
    EXPECT_FLOAT_EQ(99.0f, getEffectiveAirtimeFactor(1, 1.0f, 868.3f));
    EXPECT_FLOAT_EQ(0.0f, getEffectiveAirtimeFactor(1, 1.0f, 915.0f));
}

TEST(DutyCycleLimits, ManualKeepsTheConfiguredFactor) {
    EXPECT_FLOAT_EQ(1.0f, getEffectiveAirtimeFactor(0, 1.0f, 869.618f));
    EXPECT_FLOAT_EQ(0.5f, getEffectiveAirtimeFactor(0, 0.5f, 915.0f));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
