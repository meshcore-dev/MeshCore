#include "gps_test_support.h"

#include <memory>
#include <new>
#include <random>

namespace {

constexpr size_t kLen = MicroNMEALocationProvider::NMEA_BUFFER_LEN;
constexpr size_t kGuard = MicroNMEALocationProvider::NMEA_BUFFER_GUARD;

// The library's checksum test stops at the first '*', while parseTime()
// skips a fixed nine characters. A sentence whose checksum covers only
// "GPRMC,1" therefore passes, and the bytes after the checksum are parsed as
// fields. Both sentences below are 99 characters, the longest the buffer
// holds, so the sentence NUL sits at index 99 and the last fixed-width reads
// land past it.

// Validity flag at index 97: processRMC steps onto the NUL and the latitude
// parse reads indices 99 and 100.
std::string overreadAtLatitude() {
  std::string s = withChecksum("GPRMC,1");  // 11 chars
  while (s.size() < 96) s.push_back('X');
  s += ",AB";                                // ',' @96, 'A' @97, 'B' @98
  return s;
}

// Date field at index 98: parseDate reads six bytes from index 98 (to 103)
// and then looks for the next field at index 104.
std::string overreadAtDate() {
  std::string s = withChecksum("GPRMC,1");  // idx 0..10
  s += "AAAAA";                              // rest of the 9-char time field
  s += ",A,4716.1,N,";                       // validity, latitude, hemisphere
  const std::string tail = ",W,0.1,0.2,";    // longitude end .. course
  s += std::string(98 - tail.size() - s.size(), 'X');  // longitude filler
  s += tail;
  s += "1";                                  // date field starts at idx 98
  return s;
}

// A line longer than the buffer: the library keeps overwriting its last byte.
std::string longJunkLine() { return std::string(180, '7'); }

// Parse the same bytes with a library instance whose buffer is followed by
// NUL bytes, which is what the provider's guard tail must guarantee.
struct ReferenceParse {
  char buffer[100 + 64];
  MicroNMEA nmea;
  ReferenceParse() : buffer{}, nmea(buffer, 100) {}
  void feed(const std::string& line) {
    for (char c : line) nmea.process(c);
    nmea.process('\n');
  }
};

// Build the provider in memory that is not zero, so the test does not rely on
// a freshly zeroed stack or heap to make the guard tail look clean.
struct DirtyProvider {
  alignas(MicroNMEALocationProvider) unsigned char storage[sizeof(MicroNMEALocationProvider)];
  MicroNMEALocationProvider* gps;
  explicit DirtyProvider(Stream& serial) {
    memset(storage, 0xA5, sizeof(storage));
    gps = new (storage) MicroNMEALocationProvider(serial, nullptr, -1, -1);
  }
  ~DirtyProvider() { gps->~MicroNMEALocationProvider(); }
};

// Checksum-valid sentences whose fields run up to the end of the buffer.
// Field values stay short: MicroNMEA's parseFloat() also overflows a signed
// long on 19+ digit fields, a separate library bug this suite does not cover.
std::string malformedNearEnd(std::mt19937& rng) {
  static const char alphabet[] = ",,,,AANNWWSSEE0123456789..**X";
  std::string s = withChecksum(rng() % 2 ? "GPRMC,1" : "GPGGA,1");
  const size_t length = 80 + rng() % 20;  // 80..99 characters
  size_t run = 0;
  while (s.size() < length) {
    char c = alphabet[rng() % (sizeof(alphabet) - 1)];
    run = (c >= '0' && c <= '9') ? run + 1 : 0;
    if (run > 12) c = ',', run = 0;
    s.push_back(c);
  }
  return s;
}

}  // namespace

TEST(MicroNMEAProvider, SentenceLongerThanTheBufferIsStillRejected) {
  // The guard tail must not change which sentences are accepted: a sentence
  // of 100 or more characters is truncated and fails its checksum, as before.
  std::string body = kGoodRmc;
  while (body.size() < 100 - 4) body.push_back('Z');
  const std::string sentence = withChecksum(body);
  ASSERT_EQ(100u, sentence.size());

  g_mock_millis = 1000;
  MockSerial serial;
  MicroNMEALocationProvider gps(serial, nullptr, -1, -1);
  feedAndLoop(gps, serial, sentence);
  EXPECT_FALSE(gps.isValid());
}

TEST(MicroNMEALibrary, NeverWritesPastTheLengthItIsGiven) {
  // The premise of the guard tail: MicroNMEA 2.0.6 clamps writes at
  // buffer[len - 1]. Long, truncated and malformed lines must leave the tail
  // untouched, or it would stop being a run of NUL bytes.
  std::unique_ptr<char[]> buffer(new char[kLen + kGuard]());
  MicroNMEA nmea(buffer.get(), kLen);
  std::mt19937 rng(7);
  for (int i = 0; i < 2000; ++i) {
    std::string line = (i % 3 == 0) ? longJunkLine() : malformedNearEnd(rng);
    for (size_t k = rng() % 120; k > 0; --k) line.push_back((char)(' ' + rng() % 95));
    for (char c : line) nmea.process(c);
    nmea.process((i % 2) ? '\n' : '\r');
    for (size_t k = kLen; k < kLen + kGuard; ++k) {
      ASSERT_EQ(0, buffer[k]) << "tail byte " << k << " written at line " << i;
    }
  }
}

TEST(MicroNMEALibrary, ReadsStayInsideTheGuardTail) {
  // Heap buffer of exactly the size the provider reserves. With the address
  // sanitizer on, any parser read past the guard aborts this test.
  std::unique_ptr<char[]> buffer(new char[kLen + kGuard]());
  MicroNMEA nmea(buffer.get(), kLen);
  auto feed = [&nmea](const std::string& line) {
    for (char c : line) nmea.process(c);
    nmea.process('\n');
  };
  feed(overreadAtLatitude());
  feed(overreadAtDate());
  std::mt19937 rng(11);
  for (int i = 0; i < 20000; ++i) feed(malformedNearEnd(rng));
  SUCCEED();
}

TEST(MicroNMEAProvider, MalformedSentenceParsesAsIfFollowedByNulBytes) {
  // Without the guard tail the latitude parse reads the first byte of the
  // MicroNMEA object that follows the buffer (the buffer length, 100), so the
  // result depends on object layout. With it, the provider must parse the
  // same latitude as a reference whose buffer is followed by NULs, also after
  // a line long enough to have filled the whole buffer. Only raw parsed
  // values are compared: isValid() and getTimestamp() are the provider's own
  // decisions. The date-field over-read is covered by ReadsStayInsideTheGuardTail.
  for (bool after_long_line : {false, true}) {
    SCOPED_TRACE(after_long_line ? "after a long line" : "first line");
    const std::string sentence = overreadAtLatitude();
    ASSERT_EQ(99u, sentence.size());
    g_mock_millis = 1000;
    MockSerial serial;
    DirtyProvider dirty(serial);
    MicroNMEALocationProvider& gps = *dirty.gps;
    ReferenceParse reference;
    if (after_long_line) {
      feedAndLoop(gps, serial, longJunkLine());
      reference.feed(longJunkLine());
    }
    feedAndLoop(gps, serial, sentence);
    reference.feed(sentence);
    ASSERT_TRUE(reference.nmea.isValid());  // the sentence does reach the latitude parse
    EXPECT_EQ(reference.nmea.getLatitude(), gps.getLatitude());
    EXPECT_EQ(reference.nmea.getLongitude(), gps.getLongitude());
  }
}
