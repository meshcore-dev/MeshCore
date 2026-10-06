#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "helpers/asr650x/StatusLines.h"

static asr650x::StatusInput base() {
  asr650x::StatusInput in;
  std::memset(&in, 0, sizeof(in));
  in.name = "HELTEC-M3";
  in.id4[0] = 0xab; in.id4[1] = 0x26; in.id4[2] = 0x86; in.id4[3] = 0x31;
  in.freq_khz = 920250; in.sf = 8;
  in.max_contacts = 12;
  return in;
}
static std::string line(int i, const asr650x::StatusInput& in) { char o[22]; asr650x::status_line(i, in, o); return o; }

TEST(Asr650xStatusLines, Behaves) {
  asr650x::StatusInput in = base();
  EXPECT_TRUE(line(0, in) == "HELTEC-M3");
  EXPECT_TRUE(line(1, in) == "ID ab268631");
  EXPECT_TRUE(line(2, in) == "--:--:-- no time");                          /* clock never synced */
  EXPECT_TRUE(line(3, in) == "GPS off");
  EXPECT_TRUE(line(4, in) == "LAT --");
  EXPECT_TRUE(line(5, in) == "LON --");
  EXPECT_TRUE(line(6, in) == "BAT --  CT 0/12");                         /* battery not measured yet */
  EXPECT_TRUE(line(7, in) == "920.250MHz SF8");

  in.time_valid = true; in.epoch = 1791117296u;                      /* 2026-10-04 12:34:56 UTC */
  EXPECT_TRUE(line(2, in) == "12:34:56 UTC");
  in.epoch = 1735689600u;                                            /* midnight */
  EXPECT_TRUE(line(2, in) == "00:00:00 UTC");
  in.epoch = 1735689600u + 86399u;
  EXPECT_TRUE(line(2, in) == "23:59:59 UTC");

  in.gps_state = 1; in.warming_s = 45;
  EXPECT_TRUE(line(3, in) == "GPS search 45s");
  in.warming_s = 100000;                                             /* absurd: still within 21 characters */
  EXPECT_TRUE(line(3, in).size() <= 21);
  in.gps_state = 2; in.sats = 8; in.pos_valid = true;
  EXPECT_TRUE(line(3, in) == "GPS fix 8 sats");
  in.lat_e6 = 21028511; in.lon_e6 = 105804817;
  EXPECT_TRUE(line(4, in) == "LAT  21.028511");
  EXPECT_TRUE(line(5, in) == "LON 105.804817");
  in.lat_e6 = -33860000; in.lon_e6 = -6505620;
  EXPECT_TRUE(line(4, in) == "LAT -33.860000");
  EXPECT_TRUE(line(5, in) == "LON  -6.505620");
  in.lat_e6 = -50000; in.lon_e6 = 5;                                 /* -0.05 degrees keeps its sign, tiny values pad */
  EXPECT_TRUE(line(4, in) == "LAT  -0.050000");
  EXPECT_TRUE(line(5, in) == "LON   0.000005");
  in.pos_valid = false;
  EXPECT_TRUE(line(4, in) == "LAT --");
  in.gps_state = 3;
  EXPECT_TRUE(line(3, in) == "GPS timeout");

  in.batt_mv = 3981; in.contacts = 3;
  EXPECT_TRUE(line(6, in) == "BAT 3.98V  CT 3/12");
  in.batt_mv = 5000; in.contacts = 255; in.max_contacts = 255;
  EXPECT_TRUE(line(6, in) == "BAT 5.00V  CT 255/255");
  in.batt_mv = 65535;
  EXPECT_TRUE(line(6, in).size() <= 21);

  in.freq_khz = 915000; in.sf = 12;
  EXPECT_TRUE(line(7, in) == "915.000MHz SF12");

  /* long or odd names are cut to 21 characters; control characters and a null name are safe */
  in.name = "A-VERY-LONG-NODE-NAME-THAT-DOES-NOT-FIT-ON-THE-SCREEN";
  EXPECT_TRUE(line(0, in) == "A-VERY-LONG-NODE-NAME");                   /* exactly 21 characters */
  in.name = nullptr;
  EXPECT_TRUE(line(0, in) == "");
  in.name = "tab\there\n";
  for (char c : line(0, in)) EXPECT_TRUE((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f);
  /* out-of-range line index gives an empty line */
  EXPECT_TRUE(line(8, in) == "" && line(-1, in) == "");
  for (int i = 0; i < 8; i++) EXPECT_TRUE(line(i, in).size() <= 21);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
