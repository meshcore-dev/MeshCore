// Host tests for the observer input validators shared by the CLI setters
// (src/helpers/MQTTObserverValidation.h): IATA, owner key, NTP hostname, and
// the buffer-fit check behind the #17 length validation.
#include <gtest/gtest.h>
#include <string>
#include "helpers/MQTTObserverValidation.h"

// ---- IATA: exactly 3 alphanumerics ---------------------------------------

TEST(IataValid, AcceptsThreeLetters) {
  EXPECT_TRUE(mqttIataValid("DEN"));
  EXPECT_TRUE(mqttIataValid("den"));   // case handled (setter uppercases after)
  EXPECT_TRUE(mqttIataValid("LAX"));
}

TEST(IataValid, AcceptsThreeAlphanumerics) {
  EXPECT_TRUE(mqttIataValid("D3N"));
  EXPECT_TRUE(mqttIataValid("2M0"));
}

TEST(IataValid, RejectsWrongLength) {
  EXPECT_FALSE(mqttIataValid(""));
  EXPECT_FALSE(mqttIataValid("D"));
  EXPECT_FALSE(mqttIataValid("DE"));
  EXPECT_FALSE(mqttIataValid("DENV"));
  EXPECT_FALSE(mqttIataValid("DENVER"));
}

TEST(IataValid, RejectsNonAlphanumeric) {
  EXPECT_FALSE(mqttIataValid("D-N"));   // topic separator-ish
  EXPECT_FALSE(mqttIataValid("D N"));   // space
  EXPECT_FALSE(mqttIataValid("D/N"));   // MQTT topic separator
  EXPECT_FALSE(mqttIataValid("D+N"));   // MQTT wildcard
  EXPECT_FALSE(mqttIataValid("D#N"));   // MQTT wildcard
}

TEST(IataValid, RejectsNull) {
  EXPECT_FALSE(mqttIataValid(nullptr));
}

// ---- owner key: exactly 64 hex -------------------------------------------

static std::string hexKey(int len, char fill = 'a') { return std::string(len, fill); }

TEST(OwnerKeyValid, Accepts64Hex) {
  EXPECT_TRUE(mqttOwnerKeyValid(hexKey(64, 'a').c_str()));
  EXPECT_TRUE(mqttOwnerKeyValid(hexKey(64, 'F').c_str()));
  EXPECT_TRUE(mqttOwnerKeyValid(
      "0123456789abcdefABCDEF0123456789abcdefABCDEF0123456789abcdef0123"));
}

TEST(OwnerKeyValid, RejectsWrongLength) {
  EXPECT_FALSE(mqttOwnerKeyValid(""));
  EXPECT_FALSE(mqttOwnerKeyValid(hexKey(63).c_str()));
  EXPECT_FALSE(mqttOwnerKeyValid(hexKey(65).c_str()));
}

TEST(OwnerKeyValid, RejectsNonHex) {
  std::string k = hexKey(64);
  k[10] = 'g';                 // not a hex digit
  EXPECT_FALSE(mqttOwnerKeyValid(k.c_str()));
  k[10] = 'z';
  EXPECT_FALSE(mqttOwnerKeyValid(k.c_str()));
  k[10] = ' ';
  EXPECT_FALSE(mqttOwnerKeyValid(k.c_str()));
}

TEST(OwnerKeyValid, RejectsNull) {
  EXPECT_FALSE(mqttOwnerKeyValid(nullptr));
}

// ---- NTP hostname ---------------------------------------------------------

TEST(NtpHostnameValid, AcceptsTypicalHosts) {
  EXPECT_TRUE(mqttNtpHostnameValid("pool.ntp.org"));
  EXPECT_TRUE(mqttNtpHostnameValid("time.google.com"));
  EXPECT_TRUE(mqttNtpHostnameValid("1.2.3.4"));
  EXPECT_TRUE(mqttNtpHostnameValid("a"));
}

TEST(NtpHostnameValid, LengthBoundaryIs63) {
  EXPECT_TRUE(mqttNtpHostnameValid(std::string(63, 'a').c_str()));
  EXPECT_FALSE(mqttNtpHostnameValid(std::string(64, 'a').c_str()));
}

TEST(NtpHostnameValid, RejectsEmptyAndNull) {
  EXPECT_FALSE(mqttNtpHostnameValid(""));
  EXPECT_FALSE(mqttNtpHostnameValid(nullptr));
}

TEST(NtpHostnameValid, RejectsLeadingOrTrailingDot) {
  EXPECT_FALSE(mqttNtpHostnameValid(".pool.ntp.org"));
  EXPECT_FALSE(mqttNtpHostnameValid("pool.ntp.org."));
}

TEST(NtpHostnameValid, RejectsInvalidChars) {
  EXPECT_FALSE(mqttNtpHostnameValid("a_b"));       // underscore
  EXPECT_FALSE(mqttNtpHostnameValid("a b"));       // space
  EXPECT_FALSE(mqttNtpHostnameValid("http://x"));  // scheme / slashes
}

// ---- buffer-fit (the #17 length check) -----------------------------------

TEST(ValueFits, FitsWhenShorterThanBuffer) {
  EXPECT_TRUE(mqttValueFits("abc", 4));     // 3 < 4 (room for NUL)
  EXPECT_TRUE(mqttValueFits("", 1));        // empty fits any 1+ buffer
}

TEST(ValueFits, RejectsWhenExactlyBufferSizeOrLonger) {
  EXPECT_FALSE(mqttValueFits("abcd", 4));   // 4 == 4, no room for NUL
  EXPECT_FALSE(mqttValueFits("abcde", 4));
  EXPECT_FALSE(mqttValueFits("x", 1));
}

TEST(ValueFits, RealBufferBoundaries) {
  // Mirrors the actual MQTTPrefs field sizes the setters pass sizeof() for.
  EXPECT_TRUE(mqttValueFits(std::string(63, 'p').c_str(), 64));   // wifi_password[64]
  EXPECT_FALSE(mqttValueFits(std::string(64, 'p').c_str(), 64));
  EXPECT_TRUE(mqttValueFits(std::string(31, 's').c_str(), 32));   // wifi_ssid[32]
  EXPECT_FALSE(mqttValueFits(std::string(32, 's').c_str(), 32));
  EXPECT_TRUE(mqttValueFits(std::string(47, 't').c_str(), 48));   // slot token[48]
  EXPECT_FALSE(mqttValueFits(std::string(48, 't').c_str(), 48));
  EXPECT_TRUE(mqttValueFits(std::string(95, 'x').c_str(), 96));   // slot topic[96]
  EXPECT_FALSE(mqttValueFits(std::string(96, 'x').c_str(), 96));
}

TEST(ValueFits, RejectsNullOrZeroBuffer) {
  EXPECT_FALSE(mqttValueFits(nullptr, 32));
  EXPECT_FALSE(mqttValueFits("abc", 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
