#include <gtest/gtest.h>
#include <helpers/sensors/GpsGuard.h>

// A fix is judged on its position alone here: 8 satellites, HDOP 0.9, no reference
// position, no earlier fix. Positions are in micro-degrees, as MicroNMEA reports them.
static bool rejectedAsCoord(long lat_e6, long lon_e6) {
  GpsGuard guard;
  guard.acceptFix(lat_e6, lon_e6, 8, 9, 1800000000UL);
  char status[64];
  guard.formatStatus(status, sizeof(status));
  return strstr(status, "rej coord") != nullptr;
}

TEST(GpsGuardRange, ordinaryPositionIsNotRejected) {
  EXPECT_FALSE(rejectedAsCoord(47268723L, -122559463L));
  EXPECT_FALSE(rejectedAsCoord(0L, 0L));
}

TEST(GpsGuardRange, limitsThemselvesAreAccepted) {
  EXPECT_FALSE(rejectedAsCoord(90000000L, 180000000L));
  EXPECT_FALSE(rejectedAsCoord(-90000000L, -180000000L));
}

TEST(GpsGuardRange, latitudeOutsideRangeIsRejected) {
  EXPECT_TRUE(rejectedAsCoord(90000001L, 0L));
  EXPECT_TRUE(rejectedAsCoord(-90000001L, 0L));
  EXPECT_TRUE(rejectedAsCoord(100666665L, 181650000L));   // what a garbled "9999.9999,N" reports
}

TEST(GpsGuardRange, longitudeOutsideRangeIsRejected) {
  EXPECT_TRUE(rejectedAsCoord(0L, 180000001L));
  EXPECT_TRUE(rejectedAsCoord(0L, -180000001L));
}

TEST(GpsGuardRange, guardOffAcceptsEverything) {
  GpsGuard guard;
  guard.setEnabled(false);
  EXPECT_TRUE(guard.acceptFix(100666665L, 181650000L, 8, 9, 1800000000UL));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
