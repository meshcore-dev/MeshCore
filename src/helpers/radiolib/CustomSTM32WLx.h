#pragma once

#include <RadioLib.h>
#include "SX126xReadHelper.h"

#define SX126X_IRQ_HEADER_VALID                     0b0000010000  //  4     4     valid LoRa header received
#define SX126X_IRQ_PREAMBLE_DETECTED           0x04

class CustomSTM32WLx : public STM32WLx {
  public:
    CustomSTM32WLx(STM32WLx_Module *mod) : STM32WLx(mod) { }

    int tryReadData(uint8_t* data, size_t max_len, size_t* out_len = nullptr) {
      return sx126xTryReadData(*this, data, max_len, out_len);
    }

    bool isReceiving() {
      uint16_t irq = getIrqFlags();
      bool detected = (irq & SX126X_IRQ_HEADER_VALID) || (irq & SX126X_IRQ_PREAMBLE_DETECTED);
      return detected;
    }
};

inline int sx126xClearAllIrqs(CustomSTM32WLx& radio) {
  return radio.clearIrqStatus(RADIOLIB_SX126X_IRQ_ALL);
}
