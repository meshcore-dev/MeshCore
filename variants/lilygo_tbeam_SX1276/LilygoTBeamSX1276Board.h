#pragma once

#include <helpers/esp32/TBeamBoard.h>

class LilygoTBeamSX1276Board : public TBeamBoard {
public:
  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0; // default for SX1276
  }
};