#include "gps_test_support.h"

#include <ctime>
#include <vector>

namespace {

struct RecordingClock : mesh::RTCClock {
  uint32_t now = 1700000000;
  const uint32_t* truth = nullptr;
  std::vector<uint32_t> writes;  // values written
  std::vector<uint32_t> truths;  // time carried by the newest RMC at each write
  std::vector<uint32_t> write_ms;  // mock millis() at each write
  uint32_t getCurrentTime() override { return now; }
  void setCurrentTime(uint32_t time) override {
    now = time;
    writes.push_back(time);
    truths.push_back(truth ? *truth : 0);
    write_ms.push_back(millis());
  }
};

// A provider with a clock, a mock UART and mock time. Each send() delivers
// one line and then runs loop() for `ms` of mock time in 100 ms steps.
// `truth` is the UTC time written in the newest RMC sentence delivered.
struct ClockRig {
  MockSerial serial;
  RecordingClock clock;
  MicroNMEALocationProvider gps;
  uint32_t truth = 0;
  ClockRig() : gps(serial, &clock, -1, -1) {
    g_mock_millis = 10000;
    clock.truth = &truth;
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 100) {
      gps.loop();
      g_mock_millis += 100;
    }
  }
  void send(const std::string& line, uint32_t carried = 0, uint32_t ms = 1000) {
    if (carried) truth = carried;
    serial.feed(line + "\r\n");
    run(ms);
  }
};

// Expected UTC seconds, computed independently of the code under test.
constexpr uint32_t k120034 = 1792238434;   // 2026-10-17 12:00:34
constexpr uint32_t k120036 = 1792238436;
constexpr uint32_t k120039 = 1792238439;
constexpr uint32_t k120047 = 1792238447;
constexpr uint32_t k235940 = 1792195180;   // 2026-10-16 23:59:40
constexpr uint32_t kMidnight = 1792195200; // 2026-10-17 00:00:00
constexpr uint32_t kLeapDay = 1835438436;  // 2028-02-29 12:00:36

// Time and date fields for a UTC second ("hhmmss.00", "ddmmyy"), formatted
// with the C library rather than the code under test.
std::string timeField(uint32_t utc) {
  time_t t = utc;
  struct tm tm;
  gmtime_r(&t, &tm);
  char out[16];
  snprintf(out, sizeof(out), "%02d%02d%02d.00", tm.tm_hour, tm.tm_min, tm.tm_sec);
  return out;
}

std::string dateField(uint32_t utc) {
  time_t t = utc;
  struct tm tm;
  gmtime_r(&t, &tm);
  char out[8];
  snprintf(out, sizeof(out), "%02d%02d%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100);
  return out;
}

std::string rmc(const std::string& time, const std::string& date, const char* status = "A") {
  return withChecksum("GPRMC," + time + "," + status +
                      ",4716.1234,N,12233.5678,W,0.12,34.5," + date + ",003.1,W,A");
}

std::string rmcAt(uint32_t utc) { return rmc(timeField(utc), dateField(utc)); }

std::string ggaAt(uint32_t utc) {
  return withChecksum("GPGGA," + timeField(utc) +
                      ",4716.1234,N,12233.5678,W,1,08,0.9,125.0,M,-17.0,M,,");
}

// RMC with the comma after the E/W field missing. The text still carries
// the right time and date, but MicroNMEA's fixed-width parse lands its date
// on an empty field and keeps the date of an earlier sentence.
std::string misalignedRmcAt(uint32_t utc) {
  return withChecksum("GPRMC," + timeField(utc) +
                      ",A,4716.1234,N,12233.5678,W0.12,34.5,," + dateField(utc) + ",003.1,W,A");
}

// RMC whose checksum covers only "GPRMC": the checksum test stops at the
// first '*', so it passes, and MicroNMEA parses none of its fields.
std::string nameOnlyChecksumRmcAt(uint32_t utc) {
  return withChecksum("GPRMC") + "," + timeField(utc) +
         ",A,4716.1234,N,12233.5678,W,0.12,34.5," + dateField(utc) + ",003.1,W,A";
}

constexpr uint32_t kResyncWindowS = 31 * 60;  // past the 30-minute resync

}  // namespace

TEST(GpsClock, CleanReceiverSetsTheClockOnceFromTheNewestSentence) {
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 8; t++) {
    rig.send(ggaAt(t), 0, 0);
    rig.send(rmcAt(t), t);
  }
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, ReceiverWithoutADateYetNeverSetsTheClock) {
  // Position before date: MicroNMEA's date fields are still 0 (upstream
  // #3422's case). There is no date in the text, so there is no sample.
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 8; t++) rig.send(rmc(timeField(t), ""));
  EXPECT_TRUE(rig.clock.writes.empty());
}

TEST(GpsClock, ChecksumMustCoverTheDate) {
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 8; t++) {
    rig.send(withChecksum("GPRMC," + timeField(t) + ",A,4716.1234,N,12233.5678,W,0.12,34.5") +
             "," + dateField(t) + ",003.1,W,A");
  }
  EXPECT_TRUE(rig.clock.writes.empty());
}

TEST(GpsClock, IncompleteOrImpossibleFieldsAreNotSamples) {
  // Each defect still parses to a plausible value if its check is missing,
  // and the seconds advance, so only the check keeps it from being written.
  struct Case {
    const char* name;
    std::string (*make)(uint32_t);
  };
  static const Case cases[] = {
      {"non-digit in the time", [](uint32_t t) {
         return rmc("120:" + timeField(t).substr(4), dateField(t)); }},  // minute "0:" = 10
      {"seven-digit time", [](uint32_t t) { return rmc(timeField(t).substr(0, 6) + "5", dateField(t)); }},
      {"dot without digits", [](uint32_t t) { return rmc(timeField(t).substr(0, 7), dateField(t)); }},
      {"non-digit fraction", [](uint32_t t) { return rmc(timeField(t).substr(0, 8) + "x", dateField(t)); }},
      {"hour 24", [](uint32_t t) { return rmc("24" + timeField(t).substr(2), dateField(t)); }},
      {"minute 60", [](uint32_t t) { return rmc("1260" + timeField(t).substr(4), dateField(t)); }},
      {"non-digit in the date", [](uint32_t t) { return rmc(timeField(t), "1:1026"); }},  // day "1:" = 20
      {"five-digit date", [](uint32_t t) { return rmc(timeField(t), "17102"); }},
      {"seven-digit date", [](uint32_t t) { return rmc(timeField(t), "1710261"); }},
      {"day 0", [](uint32_t t) { return rmc(timeField(t), "001026"); }},
      {"day 32", [](uint32_t t) { return rmc(timeField(t), "321026"); }},
      {"30 February", [](uint32_t t) { return rmc(timeField(t), "300226"); }},
      {"29 February in a common year", [](uint32_t t) { return rmc(timeField(t), "290227"); }},
      {"month 0", [](uint32_t t) { return rmc(timeField(t), "170026"); }},
      {"month 13", [](uint32_t t) { return rmc(timeField(t), "171326"); }},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    ClockRig rig;
    for (uint32_t t = k120034; t < k120034 + 8; t++) rig.send(c.make(t));
    EXPECT_TRUE(rig.clock.writes.empty());
  }
}

TEST(GpsClock, LeapSecondIsNotASample) {
  // "60" in the seconds field (a leap second) would otherwise be read as the
  // first second of the next minute, while the sentence's own second is :59.
  ClockRig rig;
  const uint32_t t58 = k120034 + 24, t59 = k120034 + 25;  // 12:00:58, 12:00:59
  rig.send(rmcAt(t58), t58);
  rig.send(rmcAt(t59), t59);
  rig.send(rmc("120060.00", dateField(t59)));
  for (uint32_t t = t59 + 2; t < t59 + 6; t++) rig.send(rmcAt(t), t);
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, VoidStatusIsNotASample) {
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 8; t++) rig.send(rmc(timeField(t), dateField(t), "V"));
  EXPECT_TRUE(rig.clock.writes.empty());
}

TEST(GpsClock, ResyncAfterMidnightUsesTheDateTheSentenceCarries) {
  // From midnight on, every sentence is misaligned, so MicroNMEA's date
  // stays at 16 Oct. The periodic resync about 30 minutes later must still
  // write 17 Oct.
  ClockRig rig;
  for (uint32_t t = k235940; t < kMidnight + kResyncWindowS; t++) {
    rig.send(t < kMidnight ? rmcAt(t) : misalignedRmcAt(t), t);
  }
  ASSERT_EQ(2u, rig.clock.writes.size());
  EXPECT_LT(rig.clock.writes[0], kMidnight);
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, SentencesWithNoParsedFieldsDoNotResyncToAFrozenTime) {
  // After the first sync every sentence's checksum covers only "GPRMC", so
  // MicroNMEA's time stops at the last parsed sentence. The resync must not
  // write that frozen time.
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + kResyncWindowS; t++) {
    rig.send(t < k120034 + 10 ? rmcAt(t) : nameOnlyChecksumRmcAt(t), t);
  }
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, OneOutlierIsNeverWrittenWhereverItFalls) {
  const uint32_t outlier = 4095921636UL;  // a 2099 date in a checksum-valid sentence
  for (int position = 0; position < 8; position++) {
    SCOPED_TRACE(position);
    ClockRig rig;
    for (int i = 0; i < 10; i++) {
      const uint32_t t = k120034 + i;
      if (i == position) rig.send(rmc(timeField(t), dateField(outlier)), 0);
      else rig.send(rmcAt(t), t);
    }
    EXPECT_FALSE(rig.clock.writes.empty());
    for (uint32_t written : rig.clock.writes) {
      EXPECT_LT(written, 2000000000UL) << "no write may carry the outlier's 2099 date";
    }
    EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
  }
}

TEST(GpsClock, SilentReceiverDoesNotReplayAnOldSample) {
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 8; t++) rig.send(rmcAt(t), t);
  ASSERT_EQ(1u, rig.clock.writes.size());
  // The receiver goes quiet past the 30-minute resync point.
  rig.run(kResyncWindowS * 1000UL);
  EXPECT_EQ(1u, rig.clock.writes.size());
  // One sentence that disagrees with the time elapsed is not enough.
  rig.send(rmcAt(k120034 + 8), k120034 + 8);
  EXPECT_EQ(1u, rig.clock.writes.size());
}

TEST(GpsClock, ResyncWaitsForAFreshSample) {
  // The receiver goes quiet five seconds before the 30-minute resync is due
  // and stays quiet for ten. Its last sample is then too old to write.
  ClockRig rig;
  uint32_t t = k120034;
  for (int i = 0; i < 10 && rig.clock.writes.empty(); i++, t++) rig.send(rmcAt(t), t);
  ASSERT_EQ(1u, rig.clock.writes.size());
  const uint32_t resync_due_ms = rig.clock.write_ms[0] + 1800000UL;
  while (g_mock_millis + 5000 < resync_due_ms) rig.send(rmcAt(t), t), t++;  // send() advances time
  rig.run(10000);
  EXPECT_EQ(1u, rig.clock.writes.size());
  for (int i = 0; i < 3; i++, t++) rig.send(rmcAt(t + 10), t + 10);  // resumes, on time
  ASSERT_EQ(2u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, SampleStaysUsableUntilALateCheck) {
  // The provider checks the clock about once a second. The third sample
  // arrives just before a check is due and the next loop() runs 1.5 s
  // later; the sample must still be written then.
  ClockRig rig;
  auto at = [&rig](uint32_t ms, uint32_t utc) {
    g_mock_millis = ms;
    if (utc) {
      rig.truth = utc;
      rig.serial.feed(rmcAt(utc) + "\r\n");
    }
    rig.gps.loop();
  };
  at(10000, k120034);       // first sample; first check, next due after 11000
  at(11100, k120034 + 1);   // second sample; check, next due after 12100
  at(12050, k120034 + 2);   // third sample, check not yet due
  EXPECT_TRUE(rig.clock.writes.empty());
  at(13550, 0);             // late check, sample is 1.5 s old
  EXPECT_EQ(std::vector<uint32_t>{k120036}, rig.clock.writes);
}

// The tests below pin the agreement rule: consecutive samples must advance
// with millis(), within two seconds.

TEST(GpsClock, StuckReceiverNeverSetsTheClock) {
  ClockRig rig;
  for (int i = 0; i < 8; i++) rig.send(rmcAt(k120034), k120034);
  EXPECT_TRUE(rig.clock.writes.empty());
}

TEST(GpsClock, TimeJumpRestartsAgreement) {
  ClockRig rig;
  for (uint32_t t : {k120034, k120034 + 1, k120034 + 11, k120034 + 12, k120034 + 13}) {
    rig.send(rmcAt(t), t);  // ten seconds ahead after one second of wall time
  }
  EXPECT_EQ(std::vector<uint32_t>{k120047}, rig.clock.writes);
}

TEST(GpsClock, BackwardStepRestartsAgreement) {
  ClockRig rig;
  for (uint32_t t : {k120034, k120034 + 1, k120034, k120034 + 1, k120034 + 2}) {
    rig.send(rmcAt(t), t);
  }
  EXPECT_EQ(std::vector<uint32_t>{k120036}, rig.clock.writes);
}

TEST(GpsClock, DeliveryJitterDoesNotBreakAgreement) {
  // Sentences arrive 0.9 s and 1.1 s apart, so whole seconds of millis()
  // and of GPS time differ by one at every other step.
  ClockRig rig;
  uint32_t t = k120034;
  for (int i = 0; i < 4; i++, t++) {
    rig.send(rmcAt(t), t, 900);
    rig.send(rmcAt(++t), t, 1100);
  }
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, LoopStallOfThreeSecondsDoesNotRestartAgreement) {
  // The loop stalls for 2.5 s while the receiver emits two more sentences;
  // both are read together, three seconds of millis() after the last one.
  ClockRig rig;
  rig.send(rmcAt(k120034), k120034);
  rig.send(rmcAt(k120034 + 1), k120034 + 1, 500);
  g_mock_millis += 2500;
  rig.serial.feed(rmcAt(k120034 + 2) + "\r\n" + rmcAt(k120034 + 3) + "\r\n");
  rig.truth = k120034 + 3;
  rig.run(1000);
  EXPECT_EQ(std::vector<uint32_t>{k120034 + 3}, rig.clock.writes);
}

TEST(GpsClock, ForcedSyncNeedsThreeFreshSamples) {
  ClockRig rig;
  for (uint32_t t = k120034; t <= k120036; t++) rig.send(rmcAt(t), t);
  ASSERT_EQ(std::vector<uint32_t>{k120036}, rig.clock.writes);
  rig.gps.syncTime();
  rig.send(rmcAt(k120036 + 1), k120036 + 1);
  rig.send(rmcAt(k120036 + 2), k120036 + 2);
  EXPECT_EQ(1u, rig.clock.writes.size());
  rig.send(rmcAt(k120039), k120039);
  EXPECT_EQ((std::vector<uint32_t>{k120036, k120039}), rig.clock.writes);
}

TEST(GpsClock, ReceiverFasterThanOneHertzStillSetsTheClock) {
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 5; t++) {
    for (const char* fraction : {".00", ".20", ".40", ".60", ".80"}) {
      std::string time = timeField(t);
      time.replace(6, 3, fraction);
      rig.send(rmc(time, dateField(t)), t, 200);
    }
  }
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, LeapDayIsAValidDate) {
  ClockRig rig;
  for (uint32_t t = kLeapDay - 2; t <= kLeapDay + 2; t++) rig.send(rmcAt(t), t);
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}

TEST(GpsClock, TimeFieldWithoutFractionIsAccepted) {
  ClockRig rig;
  for (uint32_t t = k120034; t < k120034 + 5; t++) {
    rig.send(rmc(timeField(t).substr(0, 6), dateField(t)), t);
  }
  ASSERT_EQ(1u, rig.clock.writes.size());
  EXPECT_EQ(rig.clock.truths, rig.clock.writes) << "each write must equal the time in the newest RMC";
}
