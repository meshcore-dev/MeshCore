#pragma once

#include <MeshCore.h>

namespace mesh {

constexpr uint8_t defaultRxBoostedGain() {
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  return SX126X_RX_BOOSTED_GAIN;
#else
  return 1;
#endif
#else
  return 0;
#endif
}

template <typename Radio>
void applyRadioGainSettings(Radio& radio, MainBoard& board, bool rx_boosted_gain,
                            bool fem_rx_gain, bool fem_tx_gain) {
  radio.setRxBoostedGainMode(rx_boosted_gain);
  board.setLoRaFemLnaEnabled(fem_rx_gain);
  board.setLoRaFemPaGainEnabled(fem_tx_gain);
}

template <typename Radio>
void applyDefaultRadioGainSettings(Radio& radio, MainBoard& board) {
  applyRadioGainSettings(radio, board, defaultRxBoostedGain(), true, false);
}

} // namespace mesh