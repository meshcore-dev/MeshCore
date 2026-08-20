#include <gtest/gtest.h>

#include "helpers/bridges/NRFRadioChannel.h"

// The bridge reuses the ESP-NOW `bridge.channel` pref unchanged, so a channel
// number has to mean the same frequency on both platforms: the Wi-Fi channel's
// centre frequency, expressed as the RADIO FREQUENCY register wants it (MHz
// above 2400).

TEST(NRFRadioChannel, MapsChannelOneTo2412MHz) {
  EXPECT_EQ(NRFRadioChannel::frequencyFor(1), 12);
}

TEST(NRFRadioChannel, MapsChannelSixTo2437MHz) {
  EXPECT_EQ(NRFRadioChannel::frequencyFor(6), 37);
}

TEST(NRFRadioChannel, MapsChannelThirteenTo2472MHz) {
  EXPECT_EQ(NRFRadioChannel::frequencyFor(13), 72);
}

TEST(NRFRadioChannel, MapsChannelFourteenTo2484MHz) {
  // Channel 14 breaks the 5 MHz spacing, exactly as it does in 802.11
  EXPECT_EQ(NRFRadioChannel::frequencyFor(14), 84);
}

TEST(NRFRadioChannel, RejectsAnUnsetChannel) {
  // A pref saved before bridge_channel existed reads back as 0. ESP-NOW hands that
  // to esp_wifi_set_channel(), which rejects it, and the bridge does not start.
  EXPECT_LT(NRFRadioChannel::frequencyFor(0), 0);
}

TEST(NRFRadioChannel, RejectsAChannelAboveFourteen) {
  EXPECT_LT(NRFRadioChannel::frequencyFor(15), 0);
  EXPECT_LT(NRFRadioChannel::frequencyFor(255), 0);
}

TEST(NRFRadioChannel, KeepsEveryValidChannelInsideTheRadioFrequencyRange) {
  // RADIO's FREQUENCY register covers 2400-2500 MHz, i.e. 0-100
  for (uint8_t ch = 1; ch <= 14; ch++) {
    const int freq = NRFRadioChannel::frequencyFor(ch);
    EXPECT_GE(freq, 0) << "channel " << (int)ch;
    EXPECT_LE(freq, 100) << "channel " << (int)ch;
  }
}

TEST(NRFRadioChannel, GivesEveryChannelItsOwnFrequency) {
  for (uint8_t a = 1; a <= 14; a++) {
    for (uint8_t b = (uint8_t)(a + 1); b <= 14; b++) {
      EXPECT_NE(NRFRadioChannel::frequencyFor(a), NRFRadioChannel::frequencyFor(b))
          << "channels " << (int)a << " and " << (int)b;
    }
  }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
