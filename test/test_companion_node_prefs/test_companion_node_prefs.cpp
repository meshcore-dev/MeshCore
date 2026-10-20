#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../examples/companion_radio/NodePrefs.h"

// The native target tests serialization without the board-dependent CLI backend.
bool CommonRadioPrefs::setByKey(const char*, const char*) { return false; }
bool CommonRadioPrefs::getByKey(const char*, char*, size_t) { return false; }

class ReplayStream : public Stream {
  const char* _text;
  int _pos = 0;
  int _len;

public:
  explicit ReplayStream(const char* text) : _text(text), _len(strlen(text)) { }

  int available() override { return _len - _pos; }
  int read() override { return _pos < _len ? _text[_pos++] : -1; }
  int peek() override { return _pos < _len ? _text[_pos] : -1; }
};

class CaptureStream : public Stream {
  std::string _text;

  size_t emit(long long value) {
    char text[24];
    int length = snprintf(text, sizeof(text), "%lld", value);
    return write(reinterpret_cast<const uint8_t*>(text), length);
  }

public:
  size_t write(uint8_t value) override {
    _text.push_back(static_cast<char>(value));
    return 1;
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    _text.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }

  size_t print(unsigned char value, int = DEC) override { return emit(value); }
  size_t print(int value, int = DEC) override { return emit(value); }
  size_t print(unsigned int value, int = DEC) override { return emit(value); }
  size_t print(long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long value, int = DEC) override { return emit(value); }
  size_t print(long long value, int = DEC) override { return emit(value); }
  size_t print(unsigned long long value, int = DEC) override { return emit(value); }

  size_t print(double value, int precision = 2) override {
    char text[64];
    int length = snprintf(text, sizeof(text), "%.*f", precision, value);
    return write(reinterpret_cast<const uint8_t*>(text), length);
  }

  const std::string& text() const { return _text; }
};

#if 0
// Re-enable test once we can SET fem_ values in companion
TEST(CompanionNodePrefs, RxGainSettingsRoundTripIndependently) {
  NodePrefs saved;
  saved.rx_boosted_gain = 0;
  saved.radio_fem_rxgain = 1;
  saved.radio_fem_txgain = 0;

  CaptureStream output;
  ASSERT_TRUE(saved.saveSerial(output));
  EXPECT_NE(std::string::npos, output.text().find("rxgain:0"));
  EXPECT_NE(std::string::npos, output.text().find("fem_rxgain:1"));
  EXPECT_NE(std::string::npos, output.text().find("fem_txgain:0"));

  ReplayStream input("{radio:{rxgain:1,fem_rxgain:0,fem_txgain:1}}");
  NodePrefs loaded;
  loaded.rx_boosted_gain = 0;
  loaded.radio_fem_rxgain = 1;
  loaded.radio_fem_txgain = 0;

  ASSERT_TRUE(loaded.loadSerial(input));
  EXPECT_EQ(1, loaded.rx_boosted_gain);
  EXPECT_EQ(0, loaded.radio_fem_rxgain);
  EXPECT_EQ(1, loaded.radio_fem_txgain);
}
#endif

TEST(CompanionNodePrefs, RadioProfilesRoundTripInExistingPrefs) {
  NodePrefs saved;
  ReplayStream input("{radio:{freq:915,bw:250,sf:7,cr:6,profiles:{freq:920,bw:125,sf:8,cr:5,preamble:40,mode:2,primary_pre:48,cross:2}}}");
  ASSERT_TRUE(saved.loadSerial(input));
  CaptureStream output;
  ASSERT_TRUE(saved.saveSerial(output));
  NodePrefs loaded;
  ReplayStream replay(output.text().c_str());
  ASSERT_TRUE(loaded.loadSerial(replay));
  const auto& secondary = loaded.getRadioPrefs()->profileCLI().savedSecondary();
  EXPECT_FLOAT_EQ(915, loaded.freq);
  EXPECT_FLOAT_EQ(250, loaded.bw);
  EXPECT_EQ(7, loaded.sf);
  EXPECT_EQ(6, loaded.cr);
  EXPECT_FLOAT_EQ(920, secondary.params.freq);
  EXPECT_FLOAT_EQ(125, secondary.params.bw);
  EXPECT_EQ(8, secondary.params.sf);
  EXPECT_EQ(5, secondary.params.cr);
  EXPECT_EQ(40, secondary.params.preamble);
  EXPECT_EQ(mesh::RadioProfileMode::RxTx, secondary.mode);
  EXPECT_EQ(48, loaded.getRadioPrefs()->profileCLI().savedPrimary().preamble);
  EXPECT_NE(std::string::npos, output.text().find("cross:2"));
}

TEST(CompanionNodePrefs, LegacyPrefsDefaultToOneProfileNoCrossoverAndAutomaticPreamble) {
  NodePrefs prefs;
  ReplayStream input("{radio:{freq:869.525,bw:125,sf:8,cr:5}}");
  ASSERT_TRUE(prefs.loadSerial(input));
  const auto& profiles = prefs.getRadioPrefs()->profileCLI();
  EXPECT_EQ(mesh::RadioProfileMode::Off, profiles.savedSecondary().mode);
  EXPECT_EQ(0, profiles.savedSecondary().params.preamble);
  EXPECT_EQ(0, profiles.savedPrimary().preamble);
  CaptureStream output;
  ASSERT_TRUE(prefs.saveSerial(output));
  EXPECT_NE(std::string::npos, output.text().find("cross:2"));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
