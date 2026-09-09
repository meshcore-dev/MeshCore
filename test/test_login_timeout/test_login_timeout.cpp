#include <gtest/gtest.h>

#include <helpers/LoginTimeout.h>

TEST(LoginTimeout, UsesFloodBudgetForRepeaterResponseWhenLonger) {
  EXPECT_EQ(mesh::selectDirectLoginTimeout(true, 2200, 3700), 3700);
}

TEST(LoginTimeout, KeepsDirectBudgetWhenItIsLonger) {
  EXPECT_EQ(mesh::selectDirectLoginTimeout(true, 4200, 3700), 4200);
}

TEST(LoginTimeout, KeepsDirectBudgetWhenResponseDoesNotFlood) {
  EXPECT_EQ(mesh::selectDirectLoginTimeout(false, 2200, 3700), 2200);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
