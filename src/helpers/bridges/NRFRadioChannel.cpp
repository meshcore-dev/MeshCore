#include "NRFRadioChannel.h"

int NRFRadioChannel::frequencyFor(uint8_t wifi_channel) {
  if (wifi_channel < 1 || wifi_channel > 14) return -1;

  // Channel 14 sits 12 MHz above 13 rather than the usual 5, so it is spelled out
  if (wifi_channel == 14) return 2484 - 2400;

  // Channels 1-13 are 2412 MHz + 5 MHz per channel
  return 2407 + 5 * wifi_channel - 2400;
}
