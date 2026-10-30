#include "helpers/asr650x/NmeaParser.h"

#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>
#include <string>

static asr650x::NmeaEvent feed_str(asr650x::NmeaParser &p, const std::string &s) {
  asr650x::NmeaEvent last = asr650x::NMEA_NONE;
  for (unsigned char c : s) {
    asr650x::NmeaEvent e = p.feed(c);
    if (e != asr650x::NMEA_NONE) last = e;
  }
  return last;
}

TEST(Asr650xNmeaParser, Behaves) {
  /* classic GGA example: 48 07.038 N, 11 31.000 E, 8 sats, 545.4 m */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(feed_str(p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n") ==
                asr650x::NMEA_GGA);
    const asr650x::GpsData &d = p.data();
    EXPECT_TRUE(d.pos_valid && d.fix_quality == 1 && d.sats == 8);
    EXPECT_TRUE(d.lat_e6 == 48117300 && d.lon_e6 == 11516667 && d.alt_dm == 5454);
    EXPECT_TRUE(p.sentencesOk() == 1 && p.sentencesBad() == 0);
  }
  /* 5-decimal minutes, western hemisphere (Air530 style, GN talker) */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(
        feed_str(p, "$GNGGA,092750.000,5321.68020,N,00630.33720,W,1,08,1.03,61.7,M,55.2,M,,*58\r\n") ==
        asr650x::NMEA_GGA);
    EXPECT_TRUE(p.data().lat_e6 == 53361337 && p.data().lon_e6 == -6505620 && p.data().alt_dm == 617);
  }
  /* southern / eastern */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(feed_str(p, "$GNGGA,000000.000,3351.60000,S,15112.00000,E,1,05,1.0,10.0,M,0.0,M,,*5E\r\n") ==
                asr650x::NMEA_GGA);
    EXPECT_TRUE(p.data().lat_e6 == -33860000 && p.data().lon_e6 == 151200000 && p.data().sats == 5);
  }
  /* RMC with a 2026 date */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(feed_str(p, "$GNRMC,092750.000,A,5321.68020,N,00630.33720,W,0.02,31.66,041026,,,A*53\r\n") ==
                asr650x::NMEA_RMC);
    EXPECT_TRUE(p.data().rmc_valid && p.data().time_hhmmss == 92750 && p.data().date_ddmmyy == 41026);
    EXPECT_TRUE(p.data().pos_valid && p.data().lat_e6 == 53361337);
    EXPECT_TRUE(asr650x::gpsToEpoch(p.data().date_ddmmyy, p.data().time_hhmmss) ==
                1791106070u); /* 2026-10-04 09:27:50 UTC */
  }
  /* no fix: empty fields, quality 0 / status V: never a valid position */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(feed_str(p, "$GNGGA,,,,,,0,00,99.99,,,,,,*56\r\n") == asr650x::NMEA_GGA);
    EXPECT_TRUE(!p.data().pos_valid && p.data().fix_quality == 0 && p.data().sats == 0);
    EXPECT_TRUE(feed_str(p, "$GNRMC,,V,,,,,,,,,,N*4D\r\n") == asr650x::NMEA_RMC);
    EXPECT_TRUE(!p.data().rmc_valid && !p.data().pos_valid);
  }
  /* a fix followed by a no-fix sentence invalidates the position */
  {
    asr650x::NmeaParser p;
    feed_str(p, "$GNGGA,092750.000,5321.68020,N,00630.33720,W,1,08,1.03,61.7,M,55.2,M,,*58\r\n");
    EXPECT_TRUE(p.data().pos_valid);
    feed_str(p, "$GNGGA,,,,,,0,00,99.99,,,,,,*56\r\n");
    EXPECT_TRUE(!p.data().pos_valid);
  }
  /* bad checksum, missing '*', truncated, no terminator, garbage, resync on '$' */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(feed_str(p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48\r\n") ==
                asr650x::NMEA_BAD);
    EXPECT_TRUE(!p.data().pos_valid);
    EXPECT_TRUE(feed_str(p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,\r\n") ==
                asr650x::NMEA_BAD);
    EXPECT_TRUE(feed_str(p, "$GPGGA,1235") == asr650x::NMEA_NONE); /* truncated: nothing until a terminator */
    EXPECT_TRUE(feed_str(p, "\r\n") == asr650x::NMEA_BAD); /* the truncated sentence ends here: rejected */
    EXPECT_TRUE(feed_str(p, "\x01\x02\xff garbage \x80\r\n") ==
                asr650x::NMEA_NONE); /* noise outside a sentence is ignored */
    /* a '$' in the middle of a broken sentence restarts it */
    EXPECT_TRUE(
        feed_str(p, "$GPGGA,12$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n") ==
        asr650x::NMEA_GGA);
    EXPECT_TRUE(p.data().pos_valid && p.data().lat_e6 == 48117300);
    EXPECT_TRUE(p.sentencesBad() >= 2);
  }
  /* overlong sentence: dropped, parser recovers */
  {
    asr650x::NmeaParser p;
    std::string longs = "$GPGGA,";
    longs.append(300, 'A');
    longs += "*00\r\n";
    EXPECT_TRUE(feed_str(p, longs) == asr650x::NMEA_BAD);
    EXPECT_TRUE(feed_str(p, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n") ==
                asr650x::NMEA_GGA);
  }
  /* other sentence types are valid but ignored */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(feed_str(p, "$GPGSV,1,1,00*79\r\n") == asr650x::NMEA_NONE);
    EXPECT_TRUE(p.sentencesOk() == 1);
  }
  /* sentences with non-numeric junk in numeric fields must not become a position */
  {
    asr650x::NmeaParser p;
    const char *body = "GPGGA,123519,48x7.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
    char line[128];
    EXPECT_TRUE(asr650x::nmeaMake(line, sizeof(line), body) > 0);
    feed_str(p, line);
    EXPECT_TRUE(!p.data().pos_valid);
  }

  /* checksum helper */
  {
    char out[128];
    int n = asr650x::nmeaMake(out, sizeof(out), "PCAS03,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0");
    EXPECT_TRUE(n > 0 && std::strcmp(out, "$PCAS03,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0*02\r\n") == 0);
    char small[8];
    EXPECT_TRUE(asr650x::nmeaMake(small, sizeof(small), "PCAS03,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0") == 0);
  }

  /* epoch conversion */
  EXPECT_TRUE(asr650x::gpsToEpoch(41026, 123456) == 1791117296u);  /* 2026-10-04 12:34:56 */
  EXPECT_TRUE(asr650x::gpsToEpoch(10125, 0) == 1735689600u);       /* 2025-01-01 00:00:00 */
  EXPECT_TRUE(asr650x::gpsToEpoch(280226, 235959) == 1772323199u); /* 2026-02-28 23:59:59 */
  EXPECT_TRUE(asr650x::gpsToEpoch(290228, 0) == 1835395200u);      /* 2028-02-29 (leap day) */
  EXPECT_TRUE(asr650x::gpsToEpoch(290227, 0) == 0);                /* 2027-02-29 does not exist */
  EXPECT_TRUE(asr650x::gpsToEpoch(310226, 0) == 0);                /* Feb 31 */
  EXPECT_TRUE(asr650x::gpsToEpoch(230394, 123519) == 0);           /* 2094: outside the accepted window */
  EXPECT_TRUE(asr650x::gpsToEpoch(41023, 0) == 0);                 /* 2023: before the window */
  EXPECT_TRUE(asr650x::gpsToEpoch(41026, 250000) == 0 && asr650x::gpsToEpoch(41026, 126000) == 0 &&
              asr650x::gpsToEpoch(41026, 120060) == 0);
  EXPECT_TRUE(asr650x::gpsToEpoch(0, 0) == 0 && asr650x::gpsToEpoch(41026, 0) != 0);
  /* after a GPS power cycle the previous fix must not be reported (reviewer finding: stale RMC/GGA data) */
  {
    asr650x::NmeaParser p;
    EXPECT_TRUE(
        feed_str(p, "$GNGGA,092750.000,5321.68020,N,00630.33720,W,1,08,1.03,61.7,M,55.2,M,,*58\r\n") ==
        asr650x::NMEA_GGA);
    EXPECT_TRUE(p.data().pos_valid && p.data().fix_quality == 1);
    p.resetFix();
    EXPECT_TRUE(!p.data().pos_valid && !p.data().rmc_valid && p.data().fix_quality == 0 &&
                p.data().sats == 0);
  }
}

// a sentence with a valid checksum but an impossible position must not be taken as a fix
TEST(Asr650xNmea, OutOfRangeLatLonRejected) {
  const char *bodies[] = {
    "GNGGA,092750.000,9530.00000,N,00630.33720,W,1,08,1.03,61.7,M,55.2,M,,", // latitude 95 deg
    "GNGGA,092750.000,9000.00100,S,00630.33720,W,1,08,1.03,61.7,M,55.2,M,,", // just over 90 deg
    "GNGGA,092750.000,5321.68020,N,18530.33720,E,1,08,1.03,61.7,M,55.2,M,,", // longitude 185 deg
  };
  for (const char *b : bodies) {
    asr650x::NmeaParser p;
    char line[100];
    int n = asr650x::nmeaMake(line, sizeof(line), b);
    ASSERT_GT(n, 0);
    asr650x::NmeaEvent last = asr650x::NMEA_NONE;
    for (int i = 0; i < n; i++) {
      asr650x::NmeaEvent e = p.feed((uint8_t)line[i]);
      if (e != asr650x::NMEA_NONE) last = e;
    }
    EXPECT_EQ(last, asr650x::NMEA_GGA) << b;
    EXPECT_FALSE(p.data().pos_valid) << b;
  }
  // the limits themselves are valid
  asr650x::NmeaParser p;
  char line[100];
  int n = asr650x::nmeaMake(line, sizeof(line),
                            "GNGGA,092750.000,9000.00000,S,18000.00000,W,1,08,1.03,61.7,M,55.2,M,,");
  for (int i = 0; i < n; i++)
    p.feed((uint8_t)line[i]);
  EXPECT_TRUE(p.data().pos_valid);
  EXPECT_EQ(p.data().lat_e6, -90000000);
  EXPECT_EQ(p.data().lon_e6, -180000000);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
