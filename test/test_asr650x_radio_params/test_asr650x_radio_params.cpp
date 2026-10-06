#include <algorithm>
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include "helpers/asr650x/RadioParams.h"

// Independent reference: Semtech SX1262 datasheet time-on-air (explicit header, CRC on), in microseconds.
static double ref_airtime_us(int len, double bw_khz, int sf, int cr, int preamble) {
  double tsym = (double)(1 << sf) / (bw_khz * 1000.0);                 // seconds
  int de = (tsym * 1000.0 >= 16.38) ? 1 : 0;                          // low data-rate optimisation, same rule as the Heltec driver (radio.c)
  double tpre = (preamble + 4.25) * tsym;
  double num = 8.0 * len - 4.0 * sf + 28 + 16 /*CRC*/ - 0 /*explicit header*/;
  double den = 4.0 * (sf - 2 * de);
  double npay = 8 + std::max(std::ceil(num / den) * cr, 0.0);
  return (tpre + npay * tsym) * 1e6;
}

TEST(Asr650xRadioParams, Behaves) {
  /* bandwidth mapping: exact, nearest, extremes */
  EXPECT_TRUE(asr650x::bw_index(62.5f) == 3 && asr650x::bw_index(125.0f) == 0 && asr650x::bw_index(250.0f) == 1 && asr650x::bw_index(500.0f) == 2);
  EXPECT_TRUE(asr650x::bw_index(41.67f) == 4 && asr650x::bw_index(31.25f) == 5 && asr650x::bw_index(20.83f) == 6);
  EXPECT_TRUE(asr650x::bw_index(15.63f) == 7 && asr650x::bw_index(10.42f) == 8 && asr650x::bw_index(7.81f) == 9);
  EXPECT_TRUE(asr650x::bw_index(100.0f) == 0);          // nearest of {62.5, 125} by ratio is 125 (100/62.5 = 1.6 > 125/100 = 1.25)
  EXPECT_TRUE(asr650x::bw_index(7.0f) == 9);            // below range -> smallest
  EXPECT_TRUE(asr650x::bw_index(900.0f) == 2);          // above range -> largest
  EXPECT_TRUE(asr650x::bw_index(0.0f) == 9 && asr650x::bw_index(-5.0f) == 9);   // nonsense never indexes out of the table
  EXPECT_TRUE(std::fabs(asr650x::bw_value(3) - 62.5f) < 1e-3 && std::fabs(asr650x::bw_value(0) - 125.0f) < 1e-3);

  /* coding rate / sf / preamble. cr arguments use the MeshCore convention 5..8, the result is the Heltec 1..4 */
  EXPECT_TRUE(asr650x::cr_index(5) == 1 && asr650x::cr_index(8) == 4 && asr650x::cr_index(4) == 1 && asr650x::cr_index(9) == 4 && asr650x::cr_index(0) == 1);
  EXPECT_TRUE(asr650x::sf_clamp(4) == 5 && asr650x::sf_clamp(13) == 12 && asr650x::sf_clamp(8) == 8);
  EXPECT_TRUE(asr650x::preamble_for_sf(7) == 32 && asr650x::preamble_for_sf(8) == 32 && asr650x::preamble_for_sf(9) == 16 && asr650x::preamble_for_sf(12) == 16);

  /* airtime within 1.5 ms of the independent reference, for the production setting and a spread of others */
  struct { int len; double bw; int sf, cr, pre; } cases[] = {
    {110, 62.5, 8, 5, 32}, {1, 62.5, 8, 5, 32}, {255, 62.5, 8, 5, 32}, {110, 125, 7, 5, 32},
    {110, 250, 10, 5, 16}, {50, 125, 12, 8, 16}, {255, 500, 7, 6, 32}, {20, 62.5, 11, 5, 16},
    {20, 15.63, 8, 5, 32}   /* 16.378 ms symbols: the driver keeps LDRO off */
  };
  for (auto& c : cases) {
    double ref = ref_airtime_us(c.len, c.bw, c.sf, c.cr, c.pre) / 1000.0;       // multiplier is CR+4 = 5..8
    double got = asr650x::airtime_ms(c.len, (float)c.bw, (uint8_t)c.sf, (uint8_t)c.cr, (uint16_t)c.pre);
    EXPECT_NEAR(ref, got, 1.5) << "len=" << c.len << " bw=" << c.bw << " sf=" << c.sf << " cr=" << c.cr;
  }
  EXPECT_TRUE(asr650x::airtime_ms(0, 62.5f, 8, 5, 32) > 0);        // an empty packet still has preamble + header

  /* packet score */
  EXPECT_TRUE(asr650x::packet_score(-20.0f, 8, 100) == 0.0f);      // below the SF8 threshold (-10 dB)
  EXPECT_TRUE(asr650x::packet_score(0.0f, 6, 100) == 0.0f);        // unsupported SF
  float good = asr650x::packet_score(9.0f, 8, 110);
  EXPECT_TRUE(good > 0.0f && good <= 1.0f);
  EXPECT_TRUE(asr650x::packet_score(9.0f, 8, 10) > asr650x::packet_score(9.0f, 8, 200));   // longer packets score lower
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
