#include "NRF52Board.h"

void NRF52Board::begin() {
  startup_reason = BD_STARTUP_NORMAL;
}

void NRF52BoardDCDC::begin() {
  NRF52Board::begin();

  // Enable DC/DC converter for improved power efficiency
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
  } else {
    NRF_POWER->DCDCEN = 1;
  }
}