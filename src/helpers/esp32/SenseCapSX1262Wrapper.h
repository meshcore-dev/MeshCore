#pragma once

// SenseCapSX1262Wrapper — extends CustomSX1262Wrapper with a DIO1-verified
// dispatchPendingIrq() override.
//
// On SenseCAP Indicator the SX1262 DIO1, BUSY, and touch-INT all share the
// same TCA9535 /INT line (GPIO 42).  The raw ISR in SenseCapHAL only sets a
// pending flag; this override reads Port 0 via I2C and calls setFlag() only
// when DIO1 is actually HIGH, silencing spurious triggers from BUSY
// transitions, touch events, or any other Port 0 input change.

#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "SenseCapHAL.h"

class SenseCapSX1262Wrapper : public CustomSX1262Wrapper {
public:
  SenseCapSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board)
    : CustomSX1262Wrapper(radio, board) {}

protected:
  void dispatchPendingIrq() override {
    // Access HAL via Module (Module::hal is public in RadioLib 7.x)
    auto* hal = static_cast<SenseCapHAL*>(
        static_cast<CustomSX1262*>(_radio)->getMod()->hal);
    hal->dispatchPendingIrq();
  }
};
