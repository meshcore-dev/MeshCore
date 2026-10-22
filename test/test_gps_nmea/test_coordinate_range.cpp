#include "gps_test_support.h"

namespace {

// RMC with the given latitude/longitude fields and status, valid checksum.
std::string rmcAt(const char* lat, const char* ns, const char* lon,
                  const char* ew, const char* status = "A") {
  return withChecksum(std::string("GPRMC,120034.00,") + status + "," + lat + "," +
                      ns + "," + lon + "," + ew + ",0.12,34.5,171026,003.1,W,A");
}

struct Fix {
  bool valid;
  long lat;
  long lon;
};

Fix parseWithProvider(const std::string& sentence) {
  MockSerial serial;
  MicroNMEALocationProvider gps(serial, nullptr, -1, -1);
  feedAndLoop(gps, serial, sentence);
  return {gps.isValid(), gps.getLatitude(), gps.getLongitude()};
}

bool libraryCallsItAFix(const std::string& sentence) {
  char buffer[100] = {};
  MicroNMEA nmea(buffer, sizeof(buffer));
  for (char c : sentence) nmea.process(c);
  nmea.process('\n');
  return nmea.isValid();
}

}  // namespace

TEST(MicroNMEAProviderRange, RealFixesUpToTheBoundariesAreValid) {
  const struct { const char *lat, *ns, *lon, *ew; long want_lat, want_lon; } cases[] = {
      {"4716.1234", "N", "12233.5678", "W", 47268723L, -122559463L},
      {"9000.0000", "N", "18000.0000", "E", 90000000L, 180000000L},
      {"9000.0000", "S", "18000.0000", "W", -90000000L, -180000000L},
      {"0000.0000", "N", "00000.0000", "E", 0L, 0L},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(std::string(c.lat) + c.ns + " " + c.lon + c.ew);
    const Fix fix = parseWithProvider(rmcAt(c.lat, c.ns, c.lon, c.ew));
    EXPECT_TRUE(fix.valid);
    EXPECT_EQ(c.want_lat, fix.lat);
    EXPECT_EQ(c.want_lon, fix.lon);
  }
}

TEST(MicroNMEAProviderRange, PositionsNoReceiverCanReportAreNotAFix) {
  const struct { const char *lat, *ns, *lon, *ew; } cases[] = {
      {"9000.0010", "N", "12233.5678", "W"},   // 90.0000166 N
      {"9000.0010", "S", "12233.5678", "W"},   // 90.0000166 S
      {"9930.0000", "N", "12233.5678", "W"},   // 99.5 N, inside +-180
      {"4716.1234", "N", "18000.0010", "E"},   // 180.0000166 E
      {"4716.1234", "N", "18000.0010", "W"},   // 180.0000166 W
      {"4716.1234", "N", "18500.0000", "W"},   // 185 W
  };
  for (const auto& c : cases) {
    const std::string sentence = rmcAt(c.lat, c.ns, c.lon, c.ew);
    SCOPED_TRACE(sentence);
    ASSERT_TRUE(libraryCallsItAFix(sentence));
    EXPECT_FALSE(parseWithProvider(sentence).valid);
  }
}

TEST(MicroNMEAProviderRange, ChecksumValidSentenceWithAMissingFieldIsNotAFix) {
  // The N/S field is missing. The library skips two characters of the
  // longitude instead and reads "233.5678" as degrees and minutes: 233 deg.
  const std::string sentence = withChecksum(
      "GPRMC,120034.00,A,4716.1234,12233.5678,W,0.12,34.5,171026,003.1,W,A");
  ASSERT_TRUE(libraryCallsItAFix(sentence));
  const Fix fix = parseWithProvider(sentence);
  EXPECT_GT(fix.lon < 0 ? -fix.lon : fix.lon, 180000000L);
  EXPECT_FALSE(fix.valid);
}

TEST(MicroNMEAProviderRange, NoFixIsStillNoFix) {
  EXPECT_FALSE(parseWithProvider(rmcAt("4716.1234", "N", "12233.5678", "W", "V")).valid);
}

TEST(MicroNMEAProviderRange, GgaPositionIsCheckedToo) {
  const std::string good = withChecksum(
      "GPGGA,120034.00,4716.1234,N,12233.5678,W,1,08,0.9,125.0,M,-17.0,M,,");
  const std::string bad = withChecksum(
      "GPGGA,120034.00,9930.0000,N,12233.5678,W,1,08,0.9,125.0,M,-17.0,M,,");
  EXPECT_TRUE(parseWithProvider(good).valid);
  ASSERT_TRUE(libraryCallsItAFix(bad));
  EXPECT_FALSE(parseWithProvider(bad).valid);
}
